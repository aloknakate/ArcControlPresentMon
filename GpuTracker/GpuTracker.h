#pragma once
#include <Windows.h>
#include <stdint.h>
#include <vector>
#include <map>
#include <mutex>
#include <pdh.h>
#include <pdhmsg.h>
#include <thread>

class GpuTracker
{
public:
	typedef void (*fnCallbackOnGpuChanged) (void* context, uint32_t processId, uint64_t gpuLUID, double gpuUtilization);
	GpuTracker();
	void SubscribeOnGpuChanged(fnCallbackOnGpuChanged callbackOnFpsChanged, void* context);
	void UnsubscribeOnGpuChanged(fnCallbackOnGpuChanged callbackOnFpsChanged, void* context);
	void Start();
	void Stop();

private:
	struct SubscriberOnGpuChanged
	{
		fnCallbackOnGpuChanged Callback;
		void* Context;

		SubscriberOnGpuChanged(fnCallbackOnGpuChanged callback, void* context)
		{
			Callback = callback;
			Context = context;
		}
	};	
	std::vector <SubscriberOnGpuChanged> SubscribersOnGpuChanged;
	std::mutex SubscribersOnGpuChangedLock;
	std::thread ThreadPdhQueryLogic;

	bool KeepMonitoring;
	void ThreadEntryPdhQueryLogic();
	
	std::map<uint32_t, std::pair<uint64_t, double>> PID2_LUID_PERCENT;
	std::mutex PID2_LUID_PERCENTLock;
	
	void QueryAllGpuProcesses(HANDLE hQuery, PDH_HCOUNTER pdhCounter);
	bool EndsWith(const std::string& str, const std::string& suffix);
	uint32_t ExtractPID(const std::string& str);
	uint64_t ExtractLUID(const std::string& str);
	void NotifySubscribers(const std::map<uint32_t, std::pair<uint64_t, double>>& pid2GpuUtilization);
};