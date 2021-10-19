#pragma once

#include <stdint.h>
#include <vector>
#include <mutex>
#include <thread>
#include <Windows.h>
#include <powersetting.h>

/// <summary>
/// GameDetectionLogic helps track when a game has launched and exited.
/// </summary>
class GameDetectionLogic
{
public:
	typedef void (*fnCallbackOnGameChanged) (void* context, bool isGameMode, uint32_t gamePID);

	/// <summary>
	/// Indicates if the supplied PID is the foreground process.
	/// </summary>
	static bool IsForegroundProcess(uint32_t pid);

	GameDetectionLogic();

	/// <summary>
	/// Call after construction. Implementation subscribes to Enhanced Power Managment Notification event.
	/// </summary>
	void Start();
	
	/// <summary>
	/// Call before deleting. Implementation unsubscribes to Enhanced Power Managment Notification event.
	/// </summary>
	void Stop();
	
	/// <summary>
	/// Method for subscribing to game launch/exit events.
	/// </summary>
	void SubscribeOnGameStatusChanged(fnCallbackOnGameChanged callbackOnGameChanged, void* context);
	
	/// <summary>
	/// Method for unsubscribing to game launch/exit events.
	/// </summary>
	void UnsubscribeOnGameStatusChanged(fnCallbackOnGameChanged callbackOnGameChanged, void* context);
	
	/// <summary>
	/// Indicates if Win10 GameMode is currently enabled, from an enhanced power management setting perspective.
	/// GameMode can help GameDetectionLogic know when a game is running, but the feature is not required
	/// for GameDetectionLogic to operate.
	/// </summary>
	bool GetIsGameMode();
	
	/// <summary>
	/// If a non-zero PID is returned, then it means a game was identified.
	/// Generally this means that the PID is the foreground PID and the GPU utilization exceeds a threshold.
	/// </summary>
	uint32_t GetGamePID();

	/// <summary>
	/// Returns true if the supplied PID is a gaming PID.
	/// </summary>
	bool IsGamingPID(uint32_t pid, double gpuUtilization = 0);

private:
	struct SubscriberOnGameChanged
	{
		fnCallbackOnGameChanged Callback;
		void* Context;

		SubscriberOnGameChanged(fnCallbackOnGameChanged callback, void* context)
		{
			Callback = callback;
			Context = context;
		}
	};
	std::vector <SubscriberOnGameChanged> SubscribersOnGameChanged;
	std::mutex SubscribersOnGameChangedLock;
	HANDLE HandleEPMN;
	
	bool IsGameMode;
	uint32_t GamePID;
	std::thread ThreadCheckForGameExit;
	std::mutex LockState;
	bool KeepGoing;
	
	bool CheckIfGameExited(uint32_t gamePID);
	void ThreadEntryCheckForGameExit();
	bool AttemptAssignGamePID(uint32_t pid, bool pidIsForeGround, double gpuUtilization);
	void EFFECTIVE_POWER_MODE_CALLBACK_Status(EFFECTIVE_POWER_MODE Mode);
	void NotifySubscribers(bool isGameMode, uint32_t gamePID);
	static void __stdcall EFFECTIVE_POWER_MODE_CALLBACK_Status(EFFECTIVE_POWER_MODE Mode, VOID* Context);

};