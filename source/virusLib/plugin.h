#pragma once

#include <memory>

#include "device.h"
#include "midiTypes.h"
#include "resampler.h"

namespace virusLib
{
	class Plugin
	{
	public:
		Plugin();
		virtual ~Plugin();
		void addMidiEvent(const SMidiEvent& _ev);
		void setSamplerate(float _samplerate) {}
		void process(float** _inputs, float** _outputs, size_t _count);
		void setBlockSize(size_t _blockSize);

	private:
		std::vector<SMidiEvent> m_midiIn;
		std::vector<SMidiEvent> m_midiOut;
		std::unique_ptr<Device> m_device;
	};
}
