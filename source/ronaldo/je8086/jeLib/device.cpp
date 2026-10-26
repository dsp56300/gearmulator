#include "device.h"

#include "je8086.h"
#include "rom.h"
#include "synthLib/deviceException.h"
#include "synthLib/midiToSysex.h"

#include "baseLib/filesystem.h"
#include "dsp56kBase/logging.h"

#include "common/boardThread.h"

#include <algorithm>
#include <cmath>

namespace
{
	inline float dspWordToFloat(const uint32_t _d)
	{
		constexpr float scale = 1.0f / 8388608.0f;
		const auto signExtended = static_cast<int32_t>(_d << 8) >> 8;
		return static_cast<float>(signExtended) * scale;
	}

	// the PCM3001 converts 16 bits, left-justified in the ESP's 24-bit word
	inline int32_t floatToAdcWord(const float _v)
	{
		const auto s = static_cast<int32_t>(std::lround(std::clamp(_v, -1.0f, 32767.0f / 32768.0f) * 32768.0f));
		return s * 256;
	}
}

namespace jeLib
{
	constexpr uint8_t g_paramPageCustom = 6;
	constexpr uint8_t g_paramIndexMasterVolume = 0;
	constexpr uint8_t g_paramIndexInputLevel = 1;	// INST/LOWER, VOCAL/UPPER at 2

	Device::Device(const synthLib::DeviceCreateParams& _params) : synthLib::Device(_params)
	{
		const auto romSize = _params.romData.size();
		if (romSize != Rom::RomSizeKeyboard && romSize != Rom::RomSizeRack)
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing, "Unsupported firmware image. The JP-8000 needs its 512 KB flash, the JP-8080 a complete 1 MB flash dump.");

		const auto ramName = romSize == Rom::RomSizeRack ? "ram_dump_8080.bin" : "ram_dump.bin";
		const auto ramDataFilename = _params.homePath.empty() ? ramName : _params.homePath + "/roms/" + ramName;

		std::vector<uint8_t> ram;
		baseLib::filesystem::readFile(ram, ramDataFilename);

		if (ram.size() != Je8086::RamSize)
		{
			ram = Je8086::createFactoryRam(_params.romData);
			if (!baseLib::filesystem::writeFile(ramDataFilename, ram))
				LOG("Failed to write " << ramDataFilename);
		}

		m_je8086.reset(new Je8086(_params.romData, ram));

		m_thread.reset(new rLib::BoardThread<Je8086>(*m_je8086, "JE8086"));

		m_paramChangedListener.set(m_frontPanel.evParam, [this](const uint8_t _page, const uint8_t _index, const int32_t& _value)
		{
			onParamChanged(_page, _index, _value);
		});

		// inform UI about default master volume and input levels
		createCustomParameterMessages(m_midiOut);
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
		return m_je8086 != nullptr;
	}

	bool Device::getState(std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		std::vector<synthLib::SMidiEvent> results;

		if (!m_state.createSystemDump(results.emplace_back(synthLib::MidiEventSource::Device)))
			results.pop_back();

		createCustomParameterMessages(results);

		if (!m_state.createTempPerformanceDumps(results))
			return false;

		for (const auto& result : results)
			_state.insert(_state.end(), result.sysex.begin(), result.sysex.end());

		_state.insert(_state.end(), m_otherModelSystemDump.begin(), m_otherModelSystemDump.end());
		return true;
	}

	bool Device::setState(const std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		if (_state.empty())
			return false;

		synthLib::SysexBuffer stateBuf(_state.begin(), _state.end());
		synthLib::SysexBufferList messages;
		synthLib::MidiToSysex::splitMultipleSysex(messages, stateBuf);

		if (messages.empty())
			return false;

		m_masterVolume = -1.0f;
		m_inputLevel = {-1, -1};
		m_otherModelSystemDump.clear();

		for (auto& message : messages)
		{
			// A ROM switch hands over the system block of the other model, whose bytes mean something else here: byte 9
			// is unused on the JP-8080 and holds OFF, which is the JP-8000's Remote Ctrl CH (BUG-10512). This model keeps
			// its own settings, the other one gets its block back when the ROM is switched back
			if (State::isSystemDump(message, !m_je8086->isRack()))
			{
				m_otherModelSystemDump = std::move(message);
				continue;
			}

			// up to 2.2.26 the master volume was F0 7D 05 <page:2> <index:2> <value:8> F7, the front panel Param frame
			// has the same payload
			if (message.size() == 16 && message[0] == 0xf0 && message[1] == 0x7d && message[2] == 0x05)
			{
				synthLib::SysexBuffer tagged(std::begin(synthLib::fp::g_header), std::end(synthLib::fp::g_header));
				tagged.push_back(static_cast<uint8_t>(synthLib::fp::Type::Param));
				tagged.insert(tagged.end(), message.begin() + 3, message.end());
				message = std::move(tagged);
			}

			synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
			e.sysex = std::move(message);

			// let the state receive it directly, the reason is that a frozen plugin is never processed and if the DSP
			// is never processed, the state will be lost
			m_state.receive(e.sysex);

			m_frontPanel.receive(e.sysex);
			m_midiIn.emplace_back(e);
		}

		// if master volume was not part of the state, set it to 1.0f to keep compatibility with older versions that did not store it
		if (m_masterVolume < 0)
			m_masterVolume = 1.0f;

		// neither were the input levels
		for (auto& level : m_inputLevel)
		{
			if (level < 0)
				level = 64;
		}

		// feed master volume and input levels to the UI directly because there is no request message for them
		createCustomParameterMessages(m_midiOut);

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

	uint64_t Device::getDspClockHz() const
	{
		return 88'000'000;
	}

	uint32_t Device::getInternalLatencyMidiToOutput() const
	{
		// 4.5 ms, set while the Legacy resampler was the only one, minus its 0.5 ms that the plugin did not report then and
		// reports on its own now (EMU-103)
		return static_cast<uint32_t>(getSamplerate() * 4.0f / 1000.0f);
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
		// the JP-8080's A/D converter: left INST/LOWER, right VOCAL/UPPER
		const std::pair<int32_t, int32_t>* input = nullptr;

		if (m_je8086->isRack())
		{
			const float gainL = static_cast<float>(m_inputLevel[0]) / 64.0f;
			const float gainR = static_cast<float>(m_inputLevel[1]) / 64.0f;

			m_input.resize(_samples);

			for (size_t i=0; i<_samples; ++i)
			{
				m_input[i].first = _inputs[0] ? floatToAdcWord(_inputs[0][i] * gainL) : 0;
				m_input[i].second = _inputs[1] ? floatToAdcWord(_inputs[1][i] * gainR) : 0;
			}

			input = m_input.data();
		}

		const auto* frames = m_thread->process(static_cast<uint32_t>(_samples), getExtraLatencySamples(), m_midiIn, m_midiOut, input);

		for (size_t i=0; i<_samples; ++i)
		{
			const auto& s = frames[i];

			_outputs[0][i] = dspWordToFloat(s.first) * m_masterVolume;
			_outputs[1][i] = dspWordToFloat(s.second) * m_masterVolume;
		}
	}

	void Device::onTransportDiscontinuity(const synthLib::SMidiEvent& _ev)
	{
		m_midiIn.emplace_back(_ev);
	}

	bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response)
	{
		// the firmware answers an RQ1 a while later
		if (auto matcher = State::createReplyMatcher(_ev.sysex))
			expectReply(_ev.source, std::move(matcher));

		// the master volume is applied here, everything goes to the board too: it consumes the panel frames on the
		// emulation thread, see Je8086::addMidiEvent
		m_frontPanel.receive(_ev.sysex);
		m_midiIn.emplace_back(_ev);
		m_state.receive(_ev);
		return true;
	}

	void Device::onParamChanged(uint8_t/* _page*/, const uint8_t _index, const int32_t _value)
	{
		if (_index == g_paramIndexMasterVolume)
			m_masterVolume = static_cast<float>(_value) * 0.01f;
		else if (_index >= g_paramIndexInputLevel && _index < g_paramIndexInputLevel + m_inputLevel.size())
			m_inputLevel[_index - g_paramIndexInputLevel] = _value;
	}

	void Device::createCustomParameterMessages(std::vector<synthLib::SMidiEvent>& _messages) const
	{
		synthLib::fp::sendParam(_messages, g_paramPageCustom, g_paramIndexMasterVolume, static_cast<int32_t>(m_masterVolume * 100.0f));

		for (size_t i=0; i<m_inputLevel.size(); ++i)
			synthLib::fp::sendParam(_messages, g_paramPageCustom, static_cast<uint8_t>(g_paramIndexInputLevel + i), m_inputLevel[i]);
	}
}
