#pragma once

#include <stdint.h>
#include <vector>
#include <mutex>
#include <thread>
#include <Windows.h>
#include <powersetting.h>
#include "basic_semaphore.h"		// because <semaphore> doesn't exist until C++20 and that's still in preview for VisualStudio

class EnduranceGamingPolicy
{
public:
	typedef void (*fnCallbackOnGameChanged) (void* context, bool isEnduranceGaming, bool isDC, uint32_t gamePID);

	EnduranceGamingPolicy();

	/// <summary>
	/// Method for subscribing to event that indicates if EnduranceGaming started/stopped.
	/// </summary>
	void SubscribeOnEnduranceGameStatusChanged(fnCallbackOnGameChanged callbackOnGameChanged, void* context);
	
	/// <summary>
	/// Method for unsubscribing to event that indicates if EnduranceGaming started/stopped.
	/// </summary>
	void UnsubscribeOnEnduranceGameStatusChanged(fnCallbackOnGameChanged callbackOnGameChanged, void* context);

	/// <summary>
	/// Returns true if EnduranceGamingLogic thread is running/active.
	/// </summary>
	/// <param name="thread_safe">indicates whether internal lock should be used to ensure thread-safety</param>
	bool IsEnduranceGamingLogicActive(bool thread_safe = true);

	/// <summary>
	/// Returns true if a the system is powered by battery/DC.
	/// </summary>
	bool IsPowerStatusDC();

	/// <summary>
	/// Instructs the instance to check the system power status and update the state accordingly.
	/// Calling this function may cause EnduranceGamingLogic to start/stop.
	/// </summary>
	void UpdatePowerStatus();

	/// <summary>
	/// Instructs the instance to enable/disable EnduranceGaming Logic.
	/// If enabled, then EG will run if power status is DC & a game is running on Intel Integrated gfx. 
	/// </summary>
	/// <param name="isEnabled"></param>
	void UpdateEnduranceGamingEnabled(bool isEnabled);

	/// <summary>
	/// Instruct the instance to update its internal state.
	/// Calling this function may cause EnduranceGamingLogic to start/stop.
	/// </summary>
	/// <param name="gamePID">non-zero value indicates a valid PID for a game</param>
	/// <param name="isIntelIntegratedGfx">true, indicates that Intel Integrated Graphics is being used</param>
	void UpdateGameStatus(uint32_t gamePID, bool isIntelIntegratedGfx);

private:
	struct SubscriberOnEnduranceGamingStatusChanged
	{
		fnCallbackOnGameChanged Callback;
		void* Context;

		SubscriberOnEnduranceGamingStatusChanged(fnCallbackOnGameChanged callback, void* context)
		{
			Callback = callback;
			Context = context;
		}
	};
	std::vector <SubscriberOnEnduranceGamingStatusChanged> SubscribersOnEnduranceGamingChanged;
	std::mutex SubscribersOnEnduranceGamingChangedLock;
	
	bool IsDC;
	bool IsEnduranceGamingEnabled;
	uint32_t GamePID;
	std::mutex LockState;
	std::thread ThreadEnduranceGamingLogic;
	bool KeepGoing;
	std::mutex LockKeepGoing;
	basic_semaphore<std::mutex, std::condition_variable> StopSignaled;
	
	void NotifySubscribers(bool isEnduranceGaming, bool isDC, uint32_t gamePID);
	void ToggleEnduranceGamingLogic(bool ensureRunningEG);
	void ThreadEntryEnduranceGamingLogic();
};