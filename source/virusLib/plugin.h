#pragma once

#include "device.h"
#include "midiTypes.h"
#include "resamplerInOut.h"

#include <memory>

namespace virusLib
{
	class Plugin
	{
	public:
		Plugin();
		virtual ~Plugin();

		void addMidiEvent(const SMidiEvent& _ev);

		void setSamplerate(float _samplerate);
		void setBlockSize(size_t _blockSize);

		void process(float** _inputs, float** _outputs, size_t _count);
		void getMidiOut(std::vector<SMidiEvent>& _midiOut);

	private:
		std::vector<SMidiEvent> m_midiIn;
		std::vector<SMidiEvent> m_midiOut;
		std::unique_ptr<Device> m_device;

		SMidiEvent m_pendingSyexInput;

		ResamplerInOut m_resampler;
		std::mutex m_lock;
	};
}
