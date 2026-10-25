#pragma once

#include <juce_events/juce_events.h>

namespace pluginLib
{
	// macOS: hands the IO workgroup of the host's audio device to the emulation threads once per second, see
	// synthLib::updateHostAudioWorkgroup. Plugin formats other than AU have no way to ask the host for its workgroup.
	// Does nothing elsewhere
	class HostAudioWorkgroup : juce::Timer
	{
	public:
		HostAudioWorkgroup();
		~HostAudioWorkgroup() override;

	private:
		void timerCallback() override;
	};
}
