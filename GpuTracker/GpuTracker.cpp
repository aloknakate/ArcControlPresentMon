// FpsTracker.cpp : Defines the functions for the static library.
//

#include "framework.h"
#include "GpuTracker.h"

#include <stdexcept>
#include <string>

GpuTracker::GpuTracker()
{
	KeepMonitoring = false;
}

void GpuTracker::SubscribeOnGpuChanged(fnCallbackOnGpuChanged callbackOnGpuChanged, void* context)
{
	std::lock_guard<std::mutex> lock(SubscribersOnGpuChangedLock);
	for (auto& sub : SubscribersOnGpuChanged)
	{
		if ((sub.Callback == callbackOnGpuChanged) &&
			(sub.Context == context))
		{
			throw std::runtime_error("Duplicate callback/context pair found.");
		}
	}

	// allocate on the stack, and just do a shallow copy to the vector
	GpuTracker::SubscriberOnGpuChanged sub(callbackOnGpuChanged, context);
	SubscribersOnGpuChanged.push_back(sub);
}

void GpuTracker::UnsubscribeOnGpuChanged(fnCallbackOnGpuChanged callbackOnFpsChanged, void* context)
{
	std::lock_guard<std::mutex> lock(SubscribersOnGpuChangedLock);
	for (auto sub = SubscribersOnGpuChanged.begin(); sub != SubscribersOnGpuChanged.end(); ++sub)
	{
		if ((sub->Callback == callbackOnFpsChanged) &&
			(sub->Context == context))
		{
			SubscribersOnGpuChanged.erase(sub);
			break;
		}
	}
}

void GpuTracker::Start()
{
	if (KeepMonitoring) throw std::runtime_error("GpuTracker::Start() already started. ");

	KeepMonitoring = true;
	ThreadPdhQueryLogic = std::thread(&GpuTracker::ThreadEntryPdhQueryLogic, this);
}
void GpuTracker::Stop()
{
	KeepMonitoring = false;
	ThreadPdhQueryLogic.join();
}

void GpuTracker::ThreadEntryPdhQueryLogic()
{
	PDH_STATUS  pdhstatus = ERROR_SUCCESS;
	HANDLE hquery;
	PDH_HCOUNTER pdhcounter;

	if (pdhstatus = PdhOpenQueryA(NULL, 0, &hquery))
	{
		throw std::runtime_error("GpuTracker::PdhQueryLogic: PdhOpenQuery() failed. ");
	}

	// Specify a counter object with a wildcard for the instance.
	if (pdhstatus = PdhAddCounterA(hquery, "\\GPU Engine(*)\\Utilization Percentage", 0, &pdhcounter))
	{
		throw std::runtime_error("GpuTracker::PdhQueryLogic: PdhAddCounterA() failed. ");
	}

	while (KeepMonitoring)
	{
		if (true)
		{
			std::lock_guard<std::mutex> lock(PID2_LUID_PERCENTLock);

			QueryAllGpuProcesses(hquery, pdhcounter);

			if (PID2_LUID_PERCENT.size() > 0)
			{
				NotifySubscribers(PID2_LUID_PERCENT);
			}
		}

		if (KeepMonitoring)
		{
			Sleep(1000);
		}
	}

	PdhCloseQuery(hquery);
}

void GpuTracker::QueryAllGpuProcesses(HANDLE hQuery, PDH_HCOUNTER pdhCounter)
{
	PDH_STATUS  pdhstatus = ERROR_SUCCESS;

	PID2_LUID_PERCENT.clear();

	do
	{
		if (pdhstatus = PdhCollectQueryData(hQuery))
		{
			throw std::runtime_error("GpuTracker::PdhQueryLogic: PdhCollectQueryData() failed. ");
		}

		// get buffer size
		DWORD buffersize = 0;
		DWORD itemcount = 0;
		PDH_FMT_COUNTERVALUE_ITEM_A* pdhitems = NULL;
		pdhstatus = PdhGetFormattedCounterArrayA(pdhCounter, PDH_FMT_DOUBLE, &buffersize, &itemcount, pdhitems);
		if (PDH_MORE_DATA != pdhstatus)
		{
			throw std::runtime_error("GpuTracker::PdhQueryLogic: PdhGetFormattedCounterArrayA() failed to get buffer size. ");
		}

		// get results into buffer
		std::vector<unsigned char> buffer(buffersize);
		pdhitems = (PDH_FMT_COUNTERVALUE_ITEM_A*)(&buffer[0]);
		pdhstatus = PdhGetFormattedCounterArrayA(pdhCounter, PDH_FMT_DOUBLE, &buffersize, &itemcount, pdhitems);
		if (ERROR_SUCCESS != pdhstatus) 
		{ 
			break;
		}

		for (DWORD i = 0; i < itemcount; i++) 
		{
			std::string name = pdhitems[i].szName;
			if (EndsWith(name, "_engtype_3D"))
			{
				uint32_t pid = ExtractPID(name);
				uint64_t gpuluid = ExtractLUID(name);

				if (pid != 0)
				{
					auto it = PID2_LUID_PERCENT.find(pid);
					auto val = pdhitems[i].FmtValue.doubleValue;
					if (it == PID2_LUID_PERCENT.end())
					{
						// add a new entry
						PID2_LUID_PERCENT.insert(std::make_pair(pid, std::make_pair(gpuluid, val)));
					}
					else
					{
						// overwrite existing entry with bigger utilization value
						if (PID2_LUID_PERCENT[pid].second < val)
						{
							PID2_LUID_PERCENT[pid].first = gpuluid;
							PID2_LUID_PERCENT[pid].second = val;
						}
					}
				}
			}
		}
		pdhitems = NULL;
		buffersize = itemcount = 0;
	} while (false);
}


bool GpuTracker::EndsWith(const std::string& str, const std::string& suffix)
{
    return str.size() >= suffix.size() && 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
}

uint32_t GpuTracker::ExtractPID(const std::string& counterInstanceName)
{
	uint32_t retval = 0;
	size_t start = counterInstanceName.find("pid_") + 4;
	size_t end = counterInstanceName.find("_luid");

	if ((start == 4) && (end > 4))
	{
		std::string pidstr = counterInstanceName.substr(start, end - start);
		retval = std::stoi(pidstr);
	}

	return retval;
}

uint64_t GpuTracker::ExtractLUID(const std::string& counterInstanceName)
{
	uint64_t retval = 0;
	size_t start = counterInstanceName.find("_luid_") + 6;
	size_t end = counterInstanceName.find("_phys_");

	if ((start > 6) && (end > 6) && (end > start))
	{
		std::string luidstr = counterInstanceName.substr(start, end - start);
		int upperlower = luidstr.find("_");
		std::string luidupper = luidstr.substr(0, upperlower);
		std::string luidlower = luidstr.substr(upperlower + ((int)1));
		uint32_t upper = std::stoul(luidupper, nullptr, 16);
		uint32_t lower = std::stoul(luidlower , nullptr, 16);
		retval = upper;
		retval = retval << 32;
		retval = retval | (lower);
	}

	return retval;
}

void GpuTracker::NotifySubscribers(const std::map<uint32_t, std::pair<uint64_t, double>>& pid2GpuUtilization)
{
	std::lock_guard<std::mutex> lock(SubscribersOnGpuChangedLock);
	for (auto& sub : SubscribersOnGpuChanged)
	{
		if (sub.Callback != nullptr)
		{
			for (const auto & pair : pid2GpuUtilization)
			{
				std::pair<uint64_t, double> data = pair.second;
				sub.Callback(sub.Context, pair.first, data.first, data.second);
			}
		}
	}
}