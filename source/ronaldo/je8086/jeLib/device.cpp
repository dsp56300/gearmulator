#include "device.h"

#include "je8086.h"
#include "jeThread.h"
#include "dsp56kEmu/audio.h"
#include "synthLib/midiToSysex.h"

namespace jeLib
{
	Device::Device(const synthLib::DeviceCreateParams& _params) : synthLib::Device(_params)
	{
		m_je8086.reset(new Je8086(_params.romData, _params.homePath + "/roms/ram_dump.bin"));
		m_thread.reset(new JeThread(*m_je8086));
	}

	Device::~Device()
	{
		m_thread.reset();
		m_je8086.reset();
	}

	float Device::getSamplerate() const
	{
		return 88200.0f;
	}

	bool Device::isValid() const
	{
		return true;
	}

	bool Device::getState(std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		std::vector<synthLib::SMidiEvent> results;

		if (!m_state.createTempPerformanceDumps(results))
			return false;

		for (const auto& result : results)
			_state.insert(_state.end(), result.sysex.begin(), result.sysex.end());
		return true;
	}

	bool Device::setState(const std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		if (_state.empty())
			return false;

		std::vector<std::vector<uint8_t>> messages;
		synthLib::MidiToSysex::splitMultipleSysex(messages, _state);

		if (messages.empty())
			return false;

		for (auto message : messages)
		{
			synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
			e.sysex = std::move(message);

			// let the state receive it directly, the reason is that a frozen plugin is never processed and if the DSP
			// is never processed, the state will be lost
			m_state.receive(e.sysex);

			m_midiIn.emplace_back(e);
		}
		
		return true;
	}

	uint32_t Device::getChannelCountIn()
	{
		return 2;
	}

	uint32_t Device::getChannelCountOut()
	{
		return 2;
	}

	bool Device::setDspClockPercent(uint32_t _percent)
	{
		return false;
	}

	uint32_t Device::getDspClockPercent() const
	{
		return 100;
	}

	uint64_t Device::getDspClockHz() const
	{
		return 88'000'000;
	}

	void Device::readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		m_je8086->readMidiOut(_midiOut);
		m_state.receive(_midiOut);
	}

	void Device::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, const size_t _samples)
	{
		m_thread->processSamples(static_cast<uint32_t>(_samples), getExtraLatencySamples(), m_midiIn, m_midiOut);
		m_midiIn.clear();

		auto& sampleBuffer = m_thread->getSampleBuffer();

		for (size_t i=0; i<_samples; ++i)
		{
			const auto s = sampleBuffer.pop_front();

			_outputs[0][i] = dsp56k::dsp2sample<float>(s.first);
			_outputs[1][i] = dsp56k::dsp2sample<float>(s.second);
		}
	}

	bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response)
	{
		m_midiIn.emplace_back(_ev);
		m_state.receive(_ev);
		return true;
	}
}
