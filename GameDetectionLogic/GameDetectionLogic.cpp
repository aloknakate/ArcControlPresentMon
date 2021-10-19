
#include "GameDetectionLogic.h"


GameDetectionLogic::GameDetectionLogic()
{
	KeepGoing = false;
	HandleEPMN = NULL;
	IsGameMode = false;
	GamePID = 0;
}

void GameDetectionLogic::Start()
{
	KeepGoing = true;
	if (FAILED(PowerRegisterForEffectivePowerModeNotifications(
		EFFECTIVE_POWER_MODE_V2,
		GameDetectionLogic::EFFECTIVE_POWER_MODE_CALLBACK_Status,
		this,
		&HandleEPMN)))
	{
		throw std::runtime_error("GameDetectionLogic::Start() : failed PowerRegisterForEffectivePowerModeNotifications(). ");
	}

	ThreadCheckForGameExit = std::thread(&GameDetectionLogic::ThreadEntryCheckForGameExit, this);
}

void GameDetectionLogic::Stop()
{
	KeepGoing = false;

	if (FAILED(PowerUnregisterFromEffectivePowerModeNotifications(HandleEPMN)))
	{
		throw std::runtime_error("GameDetectionLogic::Stop() : failed PowerUnregisterFromEffectivePowerModeNotifications(). ");
	}

	ThreadCheckForGameExit.join();
}


void GameDetectionLogic::SubscribeOnGameStatusChanged(fnCallbackOnGameChanged callbackOnGameChanged, void* context)
{
	std::lock_guard<std::mutex> lock(SubscribersOnGameChangedLock);
	for (auto& sub : SubscribersOnGameChanged)
	{
		if ((sub.Callback == callbackOnGameChanged) &&
			(sub.Context == context))
		{
			throw std::runtime_error("Duplicate callback/context pair found.");
		}
	}

	// allocate on the stack, and just do a shallow copy to the vector
	GameDetectionLogic::SubscriberOnGameChanged sub(callbackOnGameChanged, context);
	SubscribersOnGameChanged.push_back(sub);
}


void GameDetectionLogic::UnsubscribeOnGameStatusChanged(fnCallbackOnGameChanged callbackOnGameChanged, void* context)
{
	std::lock_guard<std::mutex> lock(SubscribersOnGameChangedLock);
	for (auto sub = SubscribersOnGameChanged.begin(); sub != SubscribersOnGameChanged.end(); ++sub)
	{
		if ((sub->Callback == callbackOnGameChanged) &&
			(sub->Context == context))
		{
			SubscribersOnGameChanged.erase(sub);
			break;
		}
	}
}

bool GameDetectionLogic::GetIsGameMode() { std::lock_guard<std::mutex> lock(LockState); return IsGameMode; }
uint32_t GameDetectionLogic::GetGamePID() { std::lock_guard<std::mutex> lock(LockState); return GamePID; }

bool GameDetectionLogic::IsGamingPID(uint32_t pid, double gpuUtilization)
{
	bool retval = false;

	if (true)
	{
		std::lock_guard<std::mutex> lock(LockState);

		if (GamePID == pid)
		{
			// If GamePID already matches, then we know the process hasn't changed.
			retval = true;
		}
		else
		{
			// If it doesn't match, it's possible GamePID hasn't been assigned yet.
			// It's also possible that the game process has changed.
			// For example, some games actually start a launcher first (that does some DX calls)
			// and the launcher will start a different process for the actual game.
			// In either case, the situation is handled the same.

			retval = AttemptAssignGamePID(pid, IsForegroundProcess(pid), gpuUtilization);
		}
	}

	return retval;
}

bool GameDetectionLogic::AttemptAssignGamePID(uint32_t pid, bool pidIsForeGround, double gpuUtilization)
{
	// caller needs to already acquire LockState

	bool retval = false;
	uint32_t oldpid = GamePID;

	if (IsGameMode)
	{
		// We know a game has been launched. So if this pid is the foreground, 
		// it must be the game, regardless of what happened before.

		if (pidIsForeGround)
		{
			GamePID = pid;
			retval = true;
		}
	}
	else
	{
		// If the user has disabled GameMode on the system,
		// then IsGamingMode will always be false.
		// Therefore if this pid is the foreground app 
		// and it has sufficient GPU utilization,
		// we will assume it's the game.

		if (pidIsForeGround && (gpuUtilization > 30))
		{
			GamePID = pid;
			retval = true;
		}
	}

	if (retval)
	{
		NotifySubscribers(IsGameMode, pid);
	}

	return retval;
}

void GameDetectionLogic::NotifySubscribers(bool isGameMode, uint32_t gamePID)
{
	std::lock_guard<std::mutex> lock(SubscribersOnGameChangedLock);
	for (auto& sub : SubscribersOnGameChanged)
	{
		if (sub.Callback != nullptr)
		{
			sub.Callback(sub.Context, isGameMode, gamePID);
		}
	}
}

bool GameDetectionLogic::IsForegroundProcess(uint32_t pid)
{
	HWND hwnd = GetForegroundWindow();
	if (hwnd == NULL) return false;

	DWORD foregroundPid;
	if (GetWindowThreadProcessId(hwnd, &foregroundPid) == 0) return false;

	return (foregroundPid == pid);
}

void GameDetectionLogic::EFFECTIVE_POWER_MODE_CALLBACK_Status(EFFECTIVE_POWER_MODE Mode)
{
	bool changed = false;
	bool gaming;
	uint32_t gamepid;

	if (true)
	{
		std::lock_guard<std::mutex> lock(LockState);
		gaming = IsGameMode;

		IsGameMode = (Mode == EffectivePowerModeGameMode);

		changed = (gaming != IsGameMode);

		gaming = IsGameMode;
		gamepid = GamePID;
	}

	if (changed)
	{
		NotifySubscribers(gaming, gamepid);
	}
}

void __stdcall GameDetectionLogic::EFFECTIVE_POWER_MODE_CALLBACK_Status(EFFECTIVE_POWER_MODE Mode, VOID* Context)
{
	GameDetectionLogic* gdl = (GameDetectionLogic*)Context;
	if (gdl != nullptr)
	{
		gdl->EFFECTIVE_POWER_MODE_CALLBACK_Status(Mode);
	}
}

bool GameDetectionLogic::CheckIfGameExited(uint32_t gamePID)
{
	bool retval = false;
	if (gamePID != 0)
	{
		HANDLE hprocess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, gamePID);
		if (NULL != hprocess)
		{
			DWORD ec = 0;
			if (GetExitCodeProcess(hprocess, &ec))
			{
				if (ec != STILL_ACTIVE)
				{
					retval = true;
				}
			}
			CloseHandle(hprocess);
		}
	}
	return retval;
}


void GameDetectionLogic::ThreadEntryCheckForGameExit()
{
	HANDLE hprocess = NULL;
	uint32_t gpid;

	while (KeepGoing)
	{
		bool gm = false;
		bool gameended = false;

		if (true)
		{
			std::lock_guard<std::mutex> lock(LockState);

			if (GamePID != 0)
			{
				if (CheckIfGameExited(GamePID))
				{
					GamePID = 0;
					gameended = true;
				}
			}

			gm = IsGameMode;
			gpid = GamePID;
		}

		if (true)
		{
			if (gameended)
			{
				NotifySubscribers(gm, gpid);
			}
		}

		// TODO_THREADING : I can't open a handle to the game process and wait for the exit signal because insufficient privileges.
		// Therefore, I'm stuck polling and sleep.

		Sleep(3000);
	}
}