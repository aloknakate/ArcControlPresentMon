#pragma once

#include <vector>
#include <map>
#include <string>
#include <Windows.h>	// needed for LARGE_INTEGER
#include <evntrace.h>	// needed for TRACEHANDLE

#include "..\PresentData\TraceSession.hpp"
#include "..\PresentData\PresentMonTraceConsumer.hpp"
#include "..\PresentData\MixedRealityTraceConsumer.hpp"
#include "..\PresentMon\PresentMon.hpp"
#include "..\PresentMon\LateStageReprojectionData.hpp"

class FpsTracker
{
public:
	typedef void (*fnCallbackOnFpsChanged) (void* context, uint32_t processId, int fps);

	static int MAX_FRAMS_PER_SEC;

	FpsTracker();

	void SetExcludeProcessNames(std::vector<std::string> excludeProcessNames);
	void SubscribeOnFpsChanged(fnCallbackOnFpsChanged callbackOnFpsChanged, void* context);
	void UnsubscribeOnFpsChanged(fnCallbackOnFpsChanged callbackOnFpsChanged, void* context);
	void Start();
	void Stop();

	~FpsTracker();

private:
	struct SubscriberOnFpsChanged
	{
		fnCallbackOnFpsChanged Callback;
		void* Context;

		SubscriberOnFpsChanged(fnCallbackOnFpsChanged callback, void* context)
		{
			Callback = callback;
			Context = context;
		}
	};
	std::map<uint32_t, std::unique_ptr<std::vector<uint64_t>>> PresentEventTimes;
	std::mutex PresentEventsLock;

	std::vector <SubscriberOnFpsChanged> SubscribersOnFpsChanged;
	std::mutex SubscribersOnFpsChangedLock;

	char** ExcludeProcessNames;

	void OnPresentEvent(ProcessInfo* processInfo, SwapChainData const& chain, PresentEvent const& p);
	void NotifySubscribers(uint32_t pid, int fps);
	
	static const int SESSION_NAME_SIZE = 128;
	static char SessionName[SESSION_NAME_SIZE];
	static void OnPresentEvent(void* context, ProcessInfo* processInfo, SwapChainData const& chain, PresentEvent const& p);
};
