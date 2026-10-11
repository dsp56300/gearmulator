#include "device.h"

#include "synthLib/deviceTypes.h"

#include "mqbuildconfig.h"
#include "mqhardware.h"

namespace mqLib
{
	Device::Device(const synthLib::DeviceCreateParams& _params)
		: wLib::Device(_params, m_sysexRemote)
		, m_mq(BootMode::Default, _params.romData, _params.romName, (_params.customData & 1) != 0)
		, m_state(m_mq)
		, m_sysexRemote(m_mq)
	{
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

		m_sysexRemote.handleDirtyFlags(m_customSysexOut, dirty);
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
