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
		m_midiIn.insert(m_midiIn.end(), _midiIn.begin(), _midiIn.end());

		if(!m_midiIn.empty())
		{
			size_t i = 0;

			uint32_t end = 0;
			uint32_t begin = 0;

			bool sendMidiFailed = false;

			while(i < m_midiIn.size() && !sendMidiFailed)
			{
				end = m_midiIn[i].offset;

				const auto size = end - begin;

				m_periph.getEsai().processAudioInterleaved(_inputs, _outputs, size, 2, 2, m_nextLatency);
				readMidiOut(_midiOut);

				_inputs[0] += size;
				_inputs[1] += size;
				_outputs[0] += size;
				_outputs[1] += size;

				for(size_t j=i; j<m_midiIn.size() && !sendMidiFailed; j++)
				{
					if(m_midiIn[j].offset <= static_cast<int>(end))
					{
						if(!sendMidi(m_midiIn[j], _midiOut))
						{
							if(j > 0)
							{
								m_midiIn.erase(m_midiIn.begin(), m_midiIn.begin() + j);

								for (auto& event : m_midiIn)
									event.offset = 0;
							}
							sendMidiFailed = true;							
						}
						++i;
					}
				}

				begin = end;
			}

			if(end < _size)
			{
				m_periph.getEsai().processAudioInterleaved(_inputs, _outputs, _size - end, 2, 2, m_nextLatency);
				readMidiOut(_midiOut);				
			}

			if(!sendMidiFailed)
				m_midiIn.clear();
		}
		else
		{
			m_periph.getEsai().processAudioInterleaved(_inputs, _outputs, _size, 2, 2, m_nextLatency);
			readMidiOut(_midiOut);
		}
	}

	void Device::setBlockSize(const uint32_t _size)
	{
		m_nextLatency = _size;
	}

	void Device::startDSPThread()
	{
		m_dspThread.reset(new DSPThread(*m_dsp));
	}
}
