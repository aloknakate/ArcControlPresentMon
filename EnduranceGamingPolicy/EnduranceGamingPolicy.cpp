
#include "EnduranceGamingPolicy.h"
#include <condition_variable> // for wait_until
#include <chrono> // for system_clock::now

EnduranceGamingPolicy::EnduranceGamingPolicy()
{
	IsDC = false;
	GamePID = 0;
}

void EnduranceGamingPolicy::SubscribeOnEnduranceGameStatusChanged(fnCallbackOnGameChanged callbackOnGameChanged, void* context)
{
	std::lock_guard<std::mutex> lock(SubscribersOnEnduranceGamingChangedLock);
	for (auto& sub : SubscribersOnEnduranceGamingChanged)
	{
		if ((sub.Callback == callbackOnGameChanged) &&
			(sub.Context == context))
		{
			throw std::runtime_error("Duplicate callback/context pair found.");
		}
	}

	// allocate on the stack, and just do a shallow copy to the vector
	EnduranceGamingPolicy::SubscriberOnEnduranceGamingStatusChanged sub(callbackOnGameChanged, context);
	SubscribersOnEnduranceGamingChanged.push_back(sub);
}

void EnduranceGamingPolicy::UnsubscribeOnEnduranceGameStatusChanged(fnCallbackOnGameChanged callbackOnGameChanged, void* context)
{
	std::lock_guard<std::mutex> lock(SubscribersOnEnduranceGamingChangedLock);
	for (auto sub = SubscribersOnEnduranceGamingChanged.begin(); sub != SubscribersOnEnduranceGamingChanged.end(); ++sub)
	{
		if ((sub->Callback == callbackOnGameChanged) &&
			(sub->Context == context))
		{
			SubscribersOnEnduranceGamingChanged.erase(sub);
			break;
		}
	}
}

bool EnduranceGamingPolicy::IsEnduranceGamingLogicActive(bool thread_safe)
{
	bool retval = false;

	if (thread_safe)
	{
		std::lock_guard<std::mutex> lock(LockState);
		retval = (IsDC && GamePID != 0 && IsEnduranceGamingEnabled);
	}
	else
	{
		retval = (IsDC && GamePID != 0 && IsEnduranceGamingEnabled);
	}

	return retval;
}

bool EnduranceGamingPolicy::IsPowerStatusDC() { std::lock_guard<std::mutex> lock(LockState); return IsDC; }

void EnduranceGamingPolicy::UpdatePowerStatus()
{
	bool changed = false;
	bool dc = false, eg1 = false, eg2 = false;
	uint32_t gamepid;
	if (true)
	{
		std::lock_guard<std::mutex> lock(LockState);
		eg1 = IsEnduranceGamingLogicActive(false);

		SYSTEM_POWER_STATUS powerstatus;
		bool isDC = IsDC;
		if (GetSystemPowerStatus(&powerstatus))
		{
			IsDC = (powerstatus.ACLineStatus == 0);
		}
		eg2 = IsEnduranceGamingLogicActive(false);
		dc = IsDC;
		gamepid = GamePID;
		changed = (eg1 != eg2);
	}

	if (changed)
	{
		ToggleEnduranceGamingLogic(eg2);

		NotifySubscribers(eg2, dc, gamepid);
	}
}

void EnduranceGamingPolicy::UpdateGameStatus(uint32_t gamePID, bool isIntelIntegratedGfx)
{
	bool changed = false;
	bool dc = false, eg1 = false, eg2 = false;
	uint32_t gamepid;
	if (true)
	{
		std::lock_guard<std::mutex> lock(LockState);
		eg1 = IsEnduranceGamingLogicActive(false);

		if ((isIntelIntegratedGfx) && (gamePID != 0))
		{
			// Endurance Gaming only applies for games running on intel igfx
			GamePID = gamePID;
		}
		else
		{
			GamePID = 0;
		}
		
		eg2 = IsEnduranceGamingLogicActive(false);
		dc = IsDC;
		gamepid = GamePID;
		changed = (eg1 != eg2);
	}

	if (changed)
	{
		ToggleEnduranceGamingLogic(eg2);

		NotifySubscribers(eg2, dc, gamepid);
	}
}

void EnduranceGamingPolicy::NotifySubscribers(bool isEnduranceGaming, bool isDC, uint32_t gamePID)
{
	std::lock_guard<std::mutex> lock(SubscribersOnEnduranceGamingChangedLock);
	for (auto& sub : SubscribersOnEnduranceGamingChanged)
	{
		if (sub.Callback != nullptr)
		{
			sub.Callback(sub.Context, isEnduranceGaming, isDC, gamePID);
		}
	}
}

void EnduranceGamingPolicy::ToggleEnduranceGamingLogic(bool ensureRunningEG)
{
	std::lock_guard<std::mutex> lock(LockKeepGoing);

	if (ensureRunningEG)
	{
		if (KeepGoing == false)
		{
			if (ThreadEnduranceGamingLogic.joinable())
			{
				ThreadEnduranceGamingLogic.join();
			}

			KeepGoing = true;
			ThreadEnduranceGamingLogic = std::thread(&EnduranceGamingPolicy::ThreadEntryEnduranceGamingLogic, this);
		}
	}
	else
	{
		if (KeepGoing)
		{
			KeepGoing = false;
			StopSignaled.notify();
			ThreadEnduranceGamingLogic.join();
		}
	}
}

void EnduranceGamingPolicy::UpdateEnduranceGamingEnabled(bool isEnabled)
{
	bool changed = false;
	bool dc = false, eg1 = false, eg2 = false;
	uint32_t gamepid;
	if (true)
	{
		std::lock_guard<std::mutex> lock(LockState);
		eg1 = IsEnduranceGamingLogicActive(false);

		IsEnduranceGamingEnabled = isEnabled;
		
		eg2 = IsEnduranceGamingLogicActive(false);
		dc = IsDC;
		gamepid = GamePID;
		changed = (eg1 != eg2);
	}

	if (changed)
	{
		ToggleEnduranceGamingLogic(eg2);

		NotifySubscribers(eg2, dc, gamepid);
	}
}

void EnduranceGamingPolicy::ThreadEntryEnduranceGamingLogic()
{
	//TODO: Load the PPM details. PnP team will supply an array of PPMs, ordered in a staircase model.

	//TODO: Load the lookup table. MMS team will supply a lookup table. The columns of the lookup table will be:
	//		FPS, GPU%, CurrentPPM, TimeAtCurrentPPM, NewPPM

	//TODO: Assume currentPPM, timeAtCurrentPPM are local variables
	void* currentPPM = NULL;
	void* newPPM = NULL;

	while (KeepGoing)
	{
		//TODO: get current FPS and GPU% from IPF

		//TODO: newPPM = PerformLookup(fps, gpu, currentppm, timeAtCurrentPPM)

		//TODO: look at the history of PPMs. Apply some hysteresis checking to prevent lowering of a PPM that recently caused problems.

		if (currentPPM != newPPM)
		{
			//TODO: Need to determine how this method can instruct DTT to apply a PPM.
			//TODO: TellDttToApplyPPM(newPPM) // give DTT a PPM, so it can apply the PPM payload to the system

			// since the PPM was changed, stay asleep for a little longer.
			// We want to allow the changes to take effect and see the impact of the change on GPU% and FPS.

			// Normally would sleep for 3 seconds, but sleeping for 30sec allowed me to find deadlocks/bad-behavior easier.
			StopSignaled.wait_until(std::chrono::system_clock::now() + std::chrono::seconds(30));
		}
		else
		{
			// Normally would sleep for 1 second, but sleeping for 30sec allowed me to find deadlocks/bad-behavior easier.
			StopSignaled.wait_until(std::chrono::system_clock::now() + std::chrono::seconds(30));
		}
	}
}