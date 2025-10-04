#include "device.h"

#include "je8086.h"
#include "dsp56kEmu/audio.h"

namespace jeLib
{
	Device::Device(const synthLib::DeviceCreateParams& _params) : synthLib::Device(_params)
	{
		m_je8086.reset(new Je8086(_params.romData, _params.homePath + "/roms/ram_dump.bin"));
	}

	Device::~Device()
	{
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

		if (!m_state.getState(results))
			return false;

		for (const auto& result : results)
			_state.insert(_state.end(), result.sysex.begin(), result.sysex.end());
		return true;
	}

	bool Device::setState(const std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		return false;
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

	void Device::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples)
	{
		auto& sampleBuffer = m_je8086->getSampleBuffer();

		while (true)
		{
			const auto numSamples = sampleBuffer.size();

			if (numSamples >= _samples)
				break;

			if (!m_midiIn.empty())
			{
				while (!m_midiIn.empty())
				{
					auto& e = m_midiIn.front();
					m_state.receive(e);

					if (e.offset > numSamples)
						break;

					m_je8086->addMidiEvent(e);
					m_midiIn.pop_front();
				}
			}

			m_je8086->step();
		}

		for (size_t i=0; i<_samples; ++i)
		{
			_outputs[0][i] = dsp56k::dsp2sample<float>(sampleBuffer[i].first);
			_outputs[1][i] = dsp56k::dsp2sample<float>(sampleBuffer[i].second);
		}

		m_je8086->clearSampleBuffer();

		for (auto & event : m_midiIn)
			event.offset -= static_cast<uint32_t>(_samples);
	}

	bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response)
	{
		m_midiIn.emplace_back(_ev);
		return true;
	}
}
