#pragma once

#include <juce_events/juce_events.h>

namespace pluginLib
{
	// macOS: hands the IO workgroup of the host's audio device to the emulation threads, see dsp56k::AudioWorkgroup.
	// Plugin formats other than AU have no way to ask the host for its workgroup, but a host runs its audio device in
	// its own process, the one the plugin lives in, and CoreAudio tells which device that is. Does nothing elsewhere
	class HostAudioWorkgroup : juce::Timer
	{
	public:
		HostAudioWorkgroup();
		~HostAudioWorkgroup() override;

	private:
		void timerCallback() override;
	};
}
