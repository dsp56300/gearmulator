#pragma once

#include "device.h"

#include "../synthLib/midiTypes.h"
#include "../synthLib/resamplerInOut.h"

#include <memory>

namespace virusLib
{
	class Plugin
	{
	public:
		Plugin();
		virtual ~Plugin();

		void addMidiEvent(const synthLib::SMidiEvent& _ev);

		void setSamplerate(float _samplerate);
		void setBlockSize(size_t _blockSize);

		void process(float** _inputs, float** _outputs, size_t _count);
		void getMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut);

	private:
		float* getDummyBuffer(size_t _minimumSize);
		
		std::vector<synthLib::SMidiEvent> m_midiIn;
		std::vector<synthLib::SMidiEvent> m_midiOut;
		std::unique_ptr<Device> m_device;

		synthLib::SMidiEvent m_pendingSyexInput;

		synthLib::ResamplerInOut m_resampler;
		std::mutex m_lock;

		std::vector<float> m_dummyBuffer;
	};
}
