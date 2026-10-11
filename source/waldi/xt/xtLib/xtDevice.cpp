#include "xtDevice.h"

#include "xtHardware.h"

namespace mqLib
{
	class MicroQ;
}

namespace xt
{
	Device::Device(const synthLib::DeviceCreateParams& _params)
		: wLib::Device(_params, m_sysexRemote)
		, m_xt(_params.romData, _params.romName, (_params.customData & 1) != 0)
		, m_wavePreview(m_xt), m_state(m_xt, m_wavePreview), m_sysexRemote(m_xt)
	{
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

		m_sysexRemote.handleDirtyFlags(m_customSysexOut, dirty);
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
