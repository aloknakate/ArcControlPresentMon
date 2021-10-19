// FpsTracker.cpp : Defines the functions for the static library.
//

#include "framework.h"
#include "FpsTracker.h"

#include <stdexcept>
#include <processthreadsapi.h>

int FpsTracker::MAX_FRAMS_PER_SEC = 240;
char FpsTracker::SessionName[SESSION_NAME_SIZE];

FpsTracker::FpsTracker()
	:ExcludeProcessNames()
{
	auto args = GetCommandLineArgsPtr();
	args->mConsoleOutputType = ConsoleOutput::None;
	args->mDelay = 0;
	args->mEtlFileName = nullptr;

	//set via SetExcludeProcessNames()
	//args.mExcludeProcessNames
	
	args->mHotkeyModifiers = 0;
	args->mHotkeySupport = false;
	args->mHotkeyVirtualKeyCode = 0;
	args->mIncludeWindowsMixedReality = false;
	args->mMultiCsv = false;
	args->mOutputCsvFileName = nullptr;
	args->mOutputCsvToFile = false;
	args->mOutputCsvToStdout = false;
	args->mOutputQpcTime = false;
	args->mOutputQpcTimeInSeconds = false;
	args->mScrollLockIndicator = false;

	DWORD pid = GetCurrentProcessId();
	//sprintf_s(FpsTracker::SessionName, FpsTracker::SESSION_NAME_SIZE, "FpsTracker::%d", pid);
	sprintf_s(FpsTracker::SessionName, FpsTracker::SESSION_NAME_SIZE, "FpsTracker");
	args->mSessionName = FpsTracker::SessionName;

	args->mStartTimer = false;
	args->mStopExistingSession = true;
	args->mTargetPid = 0;
	args->mTargetProcessNames.clear();
	args->mTerminateAfterTimer = false;
	args->mTerminateExisting = false;
	args->mTerminateOnProcExit = false;
	args->mTimer = 0;
	
	// we don't care about processes from other accounts (for now)
	args->mTryToElevate = false;
	
	args->mVerbosity = Verbosity::Normal;
}

void FpsTracker::SetExcludeProcessNames(std::vector<std::string> excludeProcessNames)
{
	auto args = GetCommandLineArgsPtr();
	int i = 0;

	// delete old data
	if (ExcludeProcessNames != nullptr) {
		for (auto epn : args->mExcludeProcessNames) {
			delete[] (ExcludeProcessNames[i]);
			ExcludeProcessNames[i] = nullptr;
		}
		delete[] ExcludeProcessNames;
		ExcludeProcessNames = nullptr;
	}

	// copy new data
	ExcludeProcessNames = new char* [excludeProcessNames.size()];
	args->mExcludeProcessNames.clear();
	i = 0;
	for (auto epn : excludeProcessNames) {
		size_t sz = epn.length() + 1;
		ExcludeProcessNames[i] = new char[sz];
		strcpy_s(ExcludeProcessNames[i], sz, epn.c_str());
		args->mExcludeProcessNames.push_back(ExcludeProcessNames[i]);
	}
}

void FpsTracker::SubscribeOnFpsChanged(fnCallbackOnFpsChanged callbackOnFpsChanged, void* context)
{
	std::lock_guard<std::mutex> lock(SubscribersOnFpsChangedLock);
	for (auto& sub : SubscribersOnFpsChanged)
	{
		if ((sub.Callback == callbackOnFpsChanged) &&
			(sub.Context == context))
		{
			throw std::runtime_error("Duplicate callback/context pair found.");
		}
	}

	// allocate on the stack, and just do a shallow copy to the vector
	FpsTracker::SubscriberOnFpsChanged sub(callbackOnFpsChanged, context);
	SubscribersOnFpsChanged.push_back(sub);
}

void FpsTracker::UnsubscribeOnFpsChanged(fnCallbackOnFpsChanged callbackOnFpsChanged, void* context)
{
	std::lock_guard<std::mutex> lock(SubscribersOnFpsChangedLock);
	for (auto sub = SubscribersOnFpsChanged.begin(); sub != SubscribersOnFpsChanged.end(); ++sub)
	{
		if ((sub->Callback == callbackOnFpsChanged) &&
			(sub->Context == context))
		{
			SubscribersOnFpsChanged.erase(sub);
			break;
		}
	}
}

void FpsTracker::Start()
{
	auto const& args = GetCommandLineArgs();

	// Start the ETW trace session (including consumer and output threads).
	if (!StartTraceSession()) {
		throw std::runtime_error("FpsTracker::Start() : failed StartTraceSession(). ");
	}

	SubscribeOnPresentEvent(FpsTracker::OnPresentEvent, this);
}

void FpsTracker::Stop()
{
	StopTraceSession();
}

FpsTracker::~FpsTracker()
{
	UnsubscribeOnPresentEvent(FpsTracker::OnPresentEvent, this);
	PresentEventTimes.clear();
	SubscribersOnFpsChanged.clear();
}

void FpsTracker::OnPresentEvent(ProcessInfo* processInfo, SwapChainData const& chain, PresentEvent const& p)
{
	int fpsdelta = 0;
	int oldfps = -1;
	int fps = -1;

	// dump data into vectors... DX games will be using these 2 runtimes
	if (true)
	{
		std::lock_guard<std::mutex> lock(PresentEventsLock);
		auto v = PresentEventTimes.find(p.ProcessId);
		if (v == PresentEventTimes.end()) {
			// need to create entry in map
			PresentEventTimes.insert(std::make_pair(p.ProcessId, std::unique_ptr<std::vector<uint64_t>>(new std::vector<uint64_t>())));
		}
		auto events = PresentEventTimes[p.ProcessId].get();
		oldfps = (int)events->size(); // ok to go from size_t to int 

		// remove any timestamp that is outside the 1sec time-frame
		uint64_t qpcminus1sec = p.QpcTime - QpcFrequency();
		while (events->size() > 0 && events->front() < qpcminus1sec) {
			events->erase(events->begin());
			fpsdelta--;
		}

		// add present event
		events->push_back(p.QpcTime);
		fpsdelta++;
		fps = oldfps + fpsdelta;
	}

	// report change to subscribers
	if (fpsdelta != 0)
	{
		NotifySubscribers(p.ProcessId, fps);
	}
}

void FpsTracker::NotifySubscribers(uint32_t pid, int fps)
{
	// TODO_THREADPOOL: Since this is just a prototype, I'm not going to build threadpool functionality.
	// However, in a real implementation, the correct way to do this work would be to do the following.
	// (a) do all of the following work on a different thread/threadpool
	// (b) copy all of the event subscribers into another vector, while holding onto the SubscribersOnFpsChangedLock
	// (c) releasing the lock 
	// (d) executing each of the subscriber callbacks
	// The above changes would allow functions like FpsTracker::OnPresentEvent() to return immediately.

	std::lock_guard<std::mutex> lock(SubscribersOnFpsChangedLock);
	for (auto& sub : SubscribersOnFpsChanged)
	{
		if (sub.Callback != nullptr)
		{
			sub.Callback(sub.Context, pid, fps);
		}
	}

	// TODO_CLEANUP: Since this is just a prototype, I'm not going to write a bunch of cleanup code.
	// However, in a real implementation, it would be important to clean-up stale data so that this class
	// doesn't accumulate a bunch of data in its vectors.
}

void FpsTracker::OnPresentEvent(void* context, ProcessInfo* processInfo, SwapChainData const& chain, PresentEvent const& p)
{
	FpsTracker* ft = (FpsTracker*)context;
	ft->OnPresentEvent(processInfo, chain, p);
}

