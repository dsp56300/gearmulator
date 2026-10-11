#include "xtDevice.h"

#include "xtButtons.h"
#include "xtHardware.h"
#include "xtLeds.h"

namespace mqLib
{
	class MicroQ;
}

namespace xt
{
	Device::Device(const synthLib::DeviceCreateParams& _params)
		: wLib::Device(_params)
		, m_xt(_params.romData, _params.romName, (_params.customData & 1) != 0)
		, m_wavePreview(m_xt), m_state(m_xt, m_wavePreview)
	{
		m_frontPanel.evButton.addListener([this](const uint32_t _index, const bool _pressed)
		{
			if(_index < static_cast<uint32_t>(ButtonType::Count))
				m_xt.getHardware()->getUC().setButton(static_cast<ButtonType>(_index), _pressed);
		});

		m_frontPanel.evRequestState.addListener([this](uint8_t)
		{
			sendLcd(m_customSysexOut);
			sendLeds(m_customSysexOut);
		});

		while(!m_xt.isBootCompleted())
			m_xt.process(8);

		m_state.createInitState();

		auto* hw = m_xt.getHardware();
		hw->resetMidiCounter();
	}

	float Device::getSamplerate() const
	{
		return 40000.0f;
	}

	bool Device::isValid() const
	{
		return m_xt.isValid();
	}

	bool Device::getState(std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		return m_state.getState(_state, _type);
	}

	bool Device::setState(const std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
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
		return 4;
	}

	void Device::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples)
	{
		m_state.process(static_cast<uint32_t>(_samples));

		const float* inputs[2] = {_inputs[0], _inputs[1]};
		float* outputs[4] = {_outputs[0], _outputs[1], _outputs[2], _outputs[3]};
		m_xt.process(inputs, outputs, static_cast<uint32_t>(_samples), getExtraLatencySamples());

		const auto dirty = static_cast<uint32_t>(m_xt.getDirtyFlags());

		if(dirty & static_cast<uint32_t>(Xt::DirtyFlags::Lcd))
			sendLcd(m_customSysexOut);
		if(dirty & static_cast<uint32_t>(Xt::DirtyFlags::Leds))
			sendLeds(m_customSysexOut);
	}

	void Device::sendLcd(std::vector<synthLib::SMidiEvent>& _dst) const
	{
		std::array<char, 80> lcd{};
		m_xt.readLCD(lcd);
		synthLib::fp::sendLcdText(_dst, 40, 2, reinterpret_cast<const uint8_t*>(lcd.data()));
	}

	void Device::sendLeds(std::vector<synthLib::SMidiEvent>& _dst) const
	{
		std::vector<bool> leds(static_cast<uint32_t>(LedType::Count));
		for(size_t i=0; i<leds.size(); ++i)
			leds[i] = m_xt.getLedState(static_cast<LedType>(i));
		synthLib::fp::sendLedMask(_dst, leds);
	}

	dsp56k::EsxiClock* Device::getDspEsxiClock() const
	{
		const auto& xt = const_cast<Xt&>(m_xt);
		auto* p = dynamic_cast<dsp56k::Peripherals56303*>(xt.getHardware()->getDSP().dsp().getPeriph(dsp56k::MemArea_X));
		if(!p)
			return nullptr;
		return &p->getEssiClock();
	}
}
