#pragma once

#include <mutex>

#include "../synthLib/midiTypes.h"
#include "../synthLib/resamplerInOut.h"

namespace synthLib
{
	class Device;

	class Plugin
	{
	public:
		Plugin(Device* _device);

		void addMidiEvent(const SMidiEvent& _ev);

		void setSamplerate(float _samplerate);
		void setBlockSize(size_t _blockSize);

		void process(float** _inputs, float** _outputs, size_t _count);
		void getMidiOut(std::vector<SMidiEvent>& _midiOut);

		bool isValid() const;

	private:
		float* getDummyBuffer(size_t _minimumSize);
		
		std::vector<SMidiEvent> m_midiIn;
		std::vector<SMidiEvent> m_midiOut;

		SMidiEvent m_pendingSyexInput;

		ResamplerInOut m_resampler;
		std::mutex m_lock;

		Device* const m_device;

		std::vector<float> m_dummyBuffer;
	};
}
