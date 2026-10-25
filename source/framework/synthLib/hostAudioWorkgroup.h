#pragma once

namespace synthLib
{
	// macOS: hands the IO workgroup of the host's audio device to the emulation threads, see dsp56k::AudioWorkgroup,
	// or takes it back while none runs. A host runs its audio device in its own process, the one a plugin lives in,
	// and CoreAudio tells which device that is. The host starts and stops its device and the user switches devices,
	// so call it every now and then. Asks CoreAudio, so not from an audio thread. Does nothing elsewhere
	void updateHostAudioWorkgroup();
}
