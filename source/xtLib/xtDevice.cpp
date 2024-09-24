#include "xtDevice.h"

#include "xtHardware.h"

namespace mqLib
{
	class MicroQ;
}

namespace xt
{
	Device::Device() : m_wavePreview(m_xt), m_state(m_xt, m_wavePreview), m_sysexRemote(m_xt)
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

	uint32_t Device::getChannelCountIn()
	{
		return 2;
	}

	uint32_t Device::getChannelCountOut()
	{
		return 4;
	}

	void Device::readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		m_xt.receiveMidi(m_midiOutBuffer);
		m_midiOutParser.write(m_midiOutBuffer);
		m_midiOutParser.getEvents(_midiOut);
		m_midiOutBuffer.clear();

		wLib::Responses responses;

		for (const auto& midiOut : _midiOut)
		{
			m_state.receive(responses, midiOut, State::Origin::Device);

			for (auto& response : responses)
			{
				auto& r = _midiOut.emplace_back();
				std::swap(response, r.sysex);
			}
		}

		if(!m_customSysexOut.empty())
		{
			_midiOut.insert(_midiOut.begin(), m_customSysexOut.begin(), m_customSysexOut.end());
			m_customSysexOut.clear();
		}
	}

	void Device::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples)
	{
		const float* inputs[2] = {_inputs[0], _inputs[1]};
		float* outputs[4] = {_outputs[0], _outputs[1], _outputs[2], _outputs[3]};
		m_xt.process(inputs, outputs, static_cast<uint32_t>(_samples), getExtraLatencySamples());

		const auto dirty = static_cast<uint32_t>(m_xt.getDirtyFlags());

		m_sysexRemote.handleDirtyFlags(m_customSysexOut, dirty);
	}

	bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response)
	{
		const auto& sysex = _ev.sysex;

		if (!sysex.empty())
		{
			if (m_sysexRemote.receive(m_customSysexOut, sysex))
				return true;
		}

		Responses responses;

		const auto res = m_state.receive(responses, _ev, State::Origin::External);

		for (auto& response : responses)
		{
			auto& r = _response.emplace_back();
			std::swap(response, r.sysex);
		}

		// do not forward to device if our cache was able to reply. It might have sent something to the device already on its own if a cache miss occured
		if(res)
			return true;

		if(_ev.sysex.empty())
		{
			auto e = _ev;
			e.offset += m_numSamplesProcessed + getExtraLatencySamples();
			m_xt.sendMidiEvent(e);
		}
		else
		{
			m_xt.sendMidiEvent(_ev);
		}
		return true;
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
