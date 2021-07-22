#include "device.h"

#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/memory.h"

using namespace dsp56k;

namespace synthLib
{
	Device::Device(uint32_t _memorySize, uint32_t _externalMemStartAddress)
	{
		const size_t g_requiredMemSize	= alignedSize<DSP>() + alignedSize<Memory>() + _memorySize * MemArea_COUNT * sizeof(uint32_t);

		m_buffer.resize(alignedSize(g_requiredMemSize));

		auto* buf = &m_buffer[0];
		buf = alignedAddress(buf);

		auto* bufDSP = buf;
		auto* bufMem = bufDSP + alignedSize<DSP>();
		auto* bufBuf = bufMem + alignedSize<Memory>();

		m_periph.getEsai().setCallback([this](Audio* _audio)
		{
			onAudioWritten();
		}, 0, 1);

		m_memory = new (bufMem)Memory(m_memoryValidator, _memorySize, reinterpret_cast<TWord*>(bufBuf));
		m_dsp = new (buf)DSP(*m_memory, &m_periph, &m_periph);

		if(_externalMemStartAddress)
			m_memory->setExternalMemory(_externalMemStartAddress, true);
	}

	Device::~Device()
	{
		m_dspThread.reset();

		if(m_dsp)
		{
			m_dsp->~DSP();
			m_memory->~Memory();

			m_dsp = nullptr;
			m_memory = nullptr;
		}
	}

	void Device::dummyProcess(const uint32_t _numSamples)
	{
		std::vector<float> buf;
		buf.resize(_numSamples);
		const auto ptr = &buf[0];

		float* in[2] = {ptr, ptr};
		float* out[2] = {ptr, ptr};

		std::vector<SMidiEvent> midi;

		process(in, out, _numSamples, midi, midi);
	}

	void Device::process(float** _inputs, float** _outputs, const size_t _size, const std::vector<SMidiEvent>& _midiIn, std::vector<SMidiEvent>& _midiOut)
	{
		for (const auto& ev : _midiIn)
			sendMidi(ev, _midiOut);

		const auto maxSize = getPeriph().getEsai().getAudioInputs()[0].capacity() >> 2;

		if(_size <= maxSize)
		{
			m_periph.getEsai().processAudioInterleaved(_inputs, _outputs, _size, 2, 2, m_latency);

			readMidiOut(_midiOut);
		}
		else
		{
			// The only real "host" doing stuff like this is the AU validation tool which comes with 4096 samples at 11025 Hz. We do not want to increase the ring buffer sizes of ESAI even more
			size_t remaining = _size;

			while(remaining >= maxSize)
			{
				const size_t size = std::min(maxSize, remaining);
				m_periph.getEsai().processAudioInterleaved(_inputs, _outputs, size, 2, 2, m_latency);
				readMidiOut(_midiOut);

				_inputs[0] += size;
				_inputs[1] += size;
				_outputs[0] += size;
				_outputs[1] += size;

				remaining -= size;
			}
		}		
	}

	void Device::setLatencySamples(const uint32_t _size)
	{
		m_latency = _size;
		LOG("Latency set to " << m_latency << " samples at " << getSamplerate() << " Hz");
	}

	void Device::startDSPThread()
	{
		m_dspThread.reset(new DSPThread(*m_dsp));
	}
}
