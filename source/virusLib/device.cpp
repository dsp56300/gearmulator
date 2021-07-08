#include "device.h"
#include "romfile.h"

#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/memory.h"

using namespace dsp56k;

namespace virusLib
{
	// 128k words beginning at $20000
	constexpr TWord g_externalMemStart	= 0x020000;
	constexpr TWord g_memorySize		= 0x040000;

	constexpr size_t g_requiredMemSize	= alignedSize<DSP>() + alignedSize<Memory>() + g_memorySize * MemArea_COUNT * sizeof(uint32_t);

	Device::Device(const char* _romFileName)
		: m_rom(_romFileName)
		, m_syx(m_periph.getHDI08(), m_rom)
	{
		m_buffer.resize(alignedSize(g_requiredMemSize));

		auto* buf = &m_buffer[0];
		buf = alignedAddress(buf);

		auto* bufDSP = buf;
		auto* bufMem = bufDSP + alignedSize<DSP>();
		auto* bufBuf = bufMem + alignedSize<Memory>();
		
		m_memory = new (bufMem)Memory(m_memoryValidator, g_memorySize, reinterpret_cast<TWord*>(bufBuf));
		m_dsp = new (buf)DSP(*m_memory, &m_periph, &m_periph);

		m_memory->setExternalMemory(g_externalMemStart, true);

		auto loader = m_rom.bootDSP(*m_dsp, m_periph);

		m_dspThread.reset(new DSPThread(*m_dsp));

		loader.join();

		dummyProcess(8);

		m_syx.sendInitControlCommands();

		dummyProcess(8);

		m_syx.send(Syx::PAGE_C, 0, Syx::PLAY_MODE, 2); // enable multi mode

		Syx::TPreset preset;
		
		// Send preset
//		m_rom.loadPreset(0, 93, preset);	// RepeaterJS
//		m_rom.loadPreset(0, 6, preset);		// BusysawsSV
//		m_rom.loadPreset(0, 12, preset);	// CommerseSV on Virus C
//		m_rom.loadPreset(0, 268, preset);	// CommerseSV on Virus B
//		m_rom.getSingle(0, 116, preset);	// Virus B: Choir 4 BC
		m_rom.getSingle(0, 0, preset);

		m_syx.sendSingle(0, 0, preset, false);
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

	void Device::readMidiOut(std::vector<SMidiEvent>& _midiOut)
	{
		while(m_periph.getHDI08().hasTX())
		{
			if(m_midiOutParser.append(m_periph.getHDI08().readTX()))
			{
				const auto midi = m_midiOutParser.getMidiData();
				_midiOut.insert(_midiOut.end(), midi.begin(), midi.end());
				m_midiOutParser.clearMidiData();
			}
		}
	}

	void Device::dummyProcess(uint32_t _numSamples)
	{
		std::vector<float> buf;
		buf.resize(_numSamples);
		const auto ptr = &buf[0];
		float* in[2] = {ptr, ptr};
		float* out[2] = {ptr, ptr};

		std::vector<SMidiEvent> midiIn;
		std::vector<SMidiEvent> midiOut;

		process(in, out, _numSamples, midiIn, midiOut);
	}

	void Device::process(float** _inputs, float** _outputs, const size_t _size, const std::vector<SMidiEvent>& _midiIn, std::vector<SMidiEvent>& _midiOut)
	{
		m_midiIn.insert(m_midiIn.end(), _midiIn.begin(), _midiIn.end());

		auto sendMidi =[&](const SMidiEvent& me)
		{
			if(me.sysex.empty())
			{
//				LOG("MIDI: " << std::hex << (int)me.a << " " << (int)me.b << " " << (int)me.c);
				return m_syx.sendMIDI(me.a, me.b, me.c, true);
			}

			SMidiEvent response;
			// TODO: sysex response
			if(!m_syx.sendSysex(me.sysex, true, response.sysex))
				return false;

			if(!response.sysex.empty())
				_midiOut.emplace_back(response);

			return true;
		};
		
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
						if(!sendMidi(m_midiIn[j]))
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

	void Device::setBlockSize(size_t _size)
	{
		m_nextLatency = _size;
	}
}
