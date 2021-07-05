#include "deviceStandalone.h"

#include "romfile.h"

#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/memory.h"

using namespace dsp56k;

namespace virusLib
{
	// 128k words beginning at $20000
	constexpr TWord g_externalMemStart	= 0x020000;
	constexpr TWord g_memorySize		= 0x040000;

	StandaloneDevice::StandaloneDevice(const char* _romFileName)
		: m_memory(m_memoryValidator, g_memorySize)
		, m_dsp(m_memory, &m_periph, &m_periph)
		, m_rom(_romFileName)
		, m_syx(m_periph.getHDI08(), m_rom)
	{
		m_initDone = false;
		m_memory.setExternalMemory(g_externalMemStart, true);

		auto loader = m_rom.bootDSP(m_dsp, m_periph);

		m_dspThread.reset(new DSPThread(m_dsp));

		loader.join();

		m_initThread.reset(new std::thread([&]()
		{
			std::this_thread::sleep_for(std::chrono::seconds(1));

			m_syx.sendInitControlCommands();

			// Send preset
			std::this_thread::sleep_for(std::chrono::seconds(1));
			LOG("Sending preset!")
			m_rom.loadPreset(0, 50);	// Hoppin' SV
			m_syx.sendPreset(Syx::SINGLE, m_rom.preset);
			std::this_thread::sleep_for(std::chrono::seconds(1));
			LOG("Sending MIDI")
			m_syx.sendMIDI(0x90,60,0x7f);	// Note On

			m_initDone = true;
			m_initThread->detach();
			m_initThread.reset();
		}));
	}

	StandaloneDevice::~StandaloneDevice()
	{
		m_dspThread.reset();
	}

	void StandaloneDevice::process(float* _inputs, float* _outputs, const size_t _size, const std::vector<SMidiEvent>& _midiIn, std::vector<SMidiEvent>& _midiOut)
	{
		if(m_initDone)
		{
			if (m_periph.getHDI08().hasTX()) {
				m_periph.getHDI08().readTX();  // prevent HDI08 overflow
			}

			for(size_t i=0; i<_midiIn.size(); ++i)
			{
				const auto& me = _midiIn[i];
				// TODO: midi timing
				if(me.sysex.empty())
				{
					LOG("MIDI: " << std::hex << (int)me.a << " " << (int)me.b << " " << (int)me.c);
					m_syx.sendMIDI(me.a, me.b, me.c);				
				}
				else
				{
					std::vector<uint8_t> response;
					m_syx.sendSysex(me.sysex, false, response);
					if (!response.empty()) {
						SMidiEvent ev;
						ev.sysex = response;
						_midiOut.push_back(ev);
					}
				}
			}
		}

		m_periph.getEsai().processAudioInterleavedSingle(_inputs, _outputs, _size, 2, 2, m_nextLatency);
		// TODO: midi out
	}

	void StandaloneDevice::setBlockSize(size_t _size)
	{
		m_nextLatency = _size;
	}
}
