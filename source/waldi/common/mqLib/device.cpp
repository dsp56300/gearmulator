#include "device.h"

#include "synthLib/deviceTypes.h"

#include "mqbuildconfig.h"
#include "mqhardware.h"

namespace mqLib
{
	Device::Device(const synthLib::DeviceCreateParams& _params)
		: wLib::Device(_params)
		, m_mq(BootMode::Default, _params.romData, _params.romName, (_params.customData & 1) != 0)
		, m_state(m_mq)
	{
		m_frontPanel.evButton.addListener([this](const uint32_t _index, const bool _pressed)
		{
			if(_index < static_cast<uint32_t>(Buttons::ButtonType::Count))
				m_mq.setButton(static_cast<Buttons::ButtonType>(_index), _pressed);
		});

		m_frontPanel.evEncoder.addListener([this](const uint32_t _index, const int32_t _delta)
		{
			if(_index < static_cast<uint32_t>(Buttons::Encoders::Count) && _delta)
				m_mq.rotateEncoder(static_cast<Buttons::Encoders>(_index), _delta);
		});

		m_frontPanel.evRequestState.addListener([this](uint8_t)
		{
			sendLcd(m_customSysexOut);
			sendLcdCgRam(m_customSysexOut);
			sendLeds(m_customSysexOut);
		});

		// we need to hit the play button to resume boot if the used rom is an OS update. mQ will complain about an uninitialized ROM area in this case
		m_mq.setButton(Buttons::ButtonType::Play, true);
		while(!m_mq.isBootCompleted())
			m_mq.process(8);
		m_mq.setButton(Buttons::ButtonType::Play, false);

		m_state.createInitState();

		auto* hw = m_mq.getHardware();
		hw->resetMidiCounter();
	}

	Device::~Device() = default;

	uint32_t Device::getInternalLatencyMidiToOutput() const
	{
		return 0;
	}

	uint32_t Device::getInternalLatencyInputToOutput() const
	{
		return 0;
	}

	float Device::getSamplerate() const
	{
		return 44100.0f;
	}

	bool Device::isValid() const
	{
		return m_mq.isValid();
	}

	bool Device::getState(std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		return m_state.getState(_state, _type);
	}

	bool Device::setState(const std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		if constexpr (g_pluginDemo)
			return false;

		return m_state.setState(_state, _type);
	}

	bool Device::getStateWithPendingMidi(std::vector<uint8_t>& _state, const synthLib::StateType _type, const std::vector<synthLib::SMidiEvent>& _pendingMidi)
	{
		return wLib::State::getStateWithPendingMidi(m_state, _state, _type, _pendingMidi);
	}

	uint32_t Device::getChannelCountIn()
	{
		return 2;
	}

	uint32_t Device::getChannelCountOut()
	{
		return 6;
	}

	void Device::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, const size_t _samples)
	{
		const float* inputs[2] = {_inputs[0], _inputs[1]};
		float* outputs[6] = {_outputs[0], _outputs[1], _outputs[2], _outputs[3], _outputs[4], _outputs[5]};

		m_mq.process(inputs, outputs, static_cast<uint32_t>(_samples), getExtraLatencySamples());

		const auto dirty = static_cast<uint32_t>(m_mq.getDirtyFlags());

		if(dirty & static_cast<uint32_t>(MicroQ::DirtyFlags::Lcd))
			sendLcd(m_customSysexOut);
		if(dirty & static_cast<uint32_t>(MicroQ::DirtyFlags::LcdCgRam))
			sendLcdCgRam(m_customSysexOut);
		if(dirty & static_cast<uint32_t>(MicroQ::DirtyFlags::Leds))
			sendLeds(m_customSysexOut);
	}

	void Device::sendLcd(std::vector<synthLib::SMidiEvent>& _dst)
	{
		std::array<char, 40> lcd{};
		m_mq.readLCD(lcd);
		synthLib::fp::sendLcdText(_dst, 20, 2, reinterpret_cast<const uint8_t*>(lcd.data()));
	}

	void Device::sendLcdCgRam(std::vector<synthLib::SMidiEvent>& _dst)
	{
		std::array<uint8_t, synthLib::fp::g_cgRamSize> cgRam{};
		std::array<uint8_t, 8> character{};

		for(uint32_t c=0; c<8; ++c)
		{
			m_mq.readCustomLCDCharacter(character, c);
			std::copy(character.begin(), character.end(), cgRam.begin() + c * 8);
		}

		synthLib::fp::sendLcdCgRam(_dst, cgRam);
	}

	void Device::sendLeds(std::vector<synthLib::SMidiEvent>& _dst)
	{
		std::vector<bool> leds(static_cast<uint32_t>(Leds::Led::Count));
		for(size_t i=0; i<leds.size(); ++i)
			leds[i] = m_mq.getLedState(static_cast<Leds::Led>(i));
		synthLib::fp::sendLedMask(_dst, leds);
	}

	dsp56k::EsxiClock* Device::getDspEsxiClock() const
	{
		auto& mq = const_cast<MicroQ&>(m_mq);
		auto* p = dynamic_cast<dsp56k::Peripherals56362*>(mq.getHardware()->getDSP().dsp().getPeriph(dsp56k::MemArea_X));
		if(!p)
			return nullptr;
		return &p->getEsaiClock();
	}
}
