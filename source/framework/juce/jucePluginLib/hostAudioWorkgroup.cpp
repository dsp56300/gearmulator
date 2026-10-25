#include "hostAudioWorkgroup.h"

#include "synthLib/hostAudioWorkgroup.h"

namespace pluginLib
{
	HostAudioWorkgroup::HostAudioWorkgroup()
	{
#ifdef __APPLE__
		// the host starts and stops its device and the user switches devices in the host's settings, so look again
		// every now and then
		startTimer(1000);
#endif
	}

	HostAudioWorkgroup::~HostAudioWorkgroup()
	{
		stopTimer();
	}

	void HostAudioWorkgroup::timerCallback()
	{
		synthLib::updateHostAudioWorkgroup();
	}
}
