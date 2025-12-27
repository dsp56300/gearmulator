#include "device.h"

#include "je8086.h"
#include "jeThread.h"
#include "dsp56kEmu/audio.h"
#include "synthLib/midiToSysex.h"

namespace jeLib
{
	constexpr uint8_t g_paramPageMasterVolume = 6;
	constexpr uint8_t g_paramIndexMasterVolume = 0;

	Device::Device(const synthLib::DeviceCreateParams& _params) : synthLib::Device(_params)
	{
		const auto ramDataFilename = _params.homePath.empty() ? "ram_dump.bin" : _params.homePath + "/roms/ram_dump.bin";
		m_je8086.reset(new Je8086(_params.romData, ramDataFilename));

		if (m_je8086->hasDoneFactoryReset())
		{
			m_je8086.reset();
			m_je8086.reset(new Je8086(_params.romData, ramDataFilename));
		}

		m_thread.reset(new JeThread(*m_je8086));

		m_paramChangedListener.set(m_sysexRemote.evParamChanged, [this](const uint8_t _page, const uint8_t _index, const int32_t& _value)
		{
			onParamChanged(_page, _index, _value);
		});

		m_buttonChangedListener.set(m_sysexRemote.evButtonChanged, [this](const uint32_t _buttonIndex, const bool _pressed)
		{
			m_je8086->setButton(static_cast<devices::SwitchType>(_buttonIndex), _pressed);
		});

		// inform UI about default master volume
		createMasterVolumeMessage(m_midiOut);
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

		if (!m_state.createSystemDump(results.emplace_back(synthLib::MidiEventSource::Device)))
			results.pop_back();

		createMasterVolumeMessage(results);

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

		m_masterVolume = -1.0f;

		for (auto message : messages)
		{
			synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
			e.sysex = std::move(message);

			// let the state receive it directly, the reason is that a frozen plugin is never processed and if the DSP
			// is never processed, the state will be lost
			m_state.receive(e.sysex);

			if (!m_sysexRemote.receive(e.sysex))
				m_midiIn.emplace_back(e);
		}

		// if master volume was not part of the state, set it to 1.0f to keep compatibility with older versions that did not store it
		if (m_masterVolume < 0)
			m_masterVolume = 1.0f;

		// feed master volume to the UI directly because there is no request message for it
		createMasterVolumeMessage(m_midiOut);

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

	uint32_t Device::getInternalLatencyMidiToOutput() const
	{
		return static_cast<uint32_t>(getSamplerate() * 4.5f / 1000.0f); // 4.5 ms
	}

	void Device::readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		if (_midiOut.empty())
			std::swap(m_midiOut, _midiOut);
		else
			_midiOut.insert(_midiOut.end(), m_midiOut.begin(), m_midiOut.end());

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

			_outputs[0][i] = dsp56k::dsp2sample<float>(s.first) * m_masterVolume;
			_outputs[1][i] = dsp56k::dsp2sample<float>(s.second) * m_masterVolume;
		}
	}

	bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response)
	{
		if (!m_sysexRemote.receive(_ev.sysex))
			m_midiIn.emplace_back(_ev);
		m_state.receive(_ev);
		return true;
	}

	void Device::onParamChanged(uint8_t/* _page*/, uint8_t/* _index*/, const int32_t _value)
	{
		m_masterVolume = static_cast<float>(_value) * 0.01f;
	}

	void Device::createMasterVolumeMessage(std::vector<synthLib::SMidiEvent>& _messages) const
	{
		SysexRemoteControl::sendSysexParameter(_messages, g_paramPageMasterVolume, g_paramIndexMasterVolume, static_cast<int32_t>(m_masterVolume * 100.0f));
	}
}
