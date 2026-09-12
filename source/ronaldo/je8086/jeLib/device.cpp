#include "device.h"

#include "je8086.h"

#include <cstdlib>
#include "jeThread.h"
#include "jePipeline.h"
#include "synthLib/midiToSysex.h"

namespace
{
	inline float dspWordToFloat(const uint32_t _d)
	{
		constexpr float scale = 1.0f / 8388608.0f;
		const auto signExtended = static_cast<int32_t>(_d << 8) >> 8;
		return static_cast<float>(signExtended) * scale;
	}
}

namespace jeLib
{
	/* The pipeline hands one sample over for every sample rendered, taken this
	 * many samples late. It only has to cover what is in flight; two is enough,
	 * and a constant delay is what makes the output bit-exact against serial. */
	static constexpr int64_t g_pipelineDelaySamples = 2;

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

		/* Opt in to the parallel ASIC pipeline (see jePipeline.h). Off unless asked
		 * for, and only worth asking for where one core cannot render the chain in
		 * real time -- an SBC, not a desktop. Measured on a Pi 4 through a CLAP
		 * host: 0.7x real time without it, which is unusable; 1.8x with it.
		 *
		 * The count arrives through DeviceCreateParams, which the plugin fills in
		 * from its own settings. It has to be decided here, before the engine
		 * thread exists, because the pipeline delivers audio on a fixed delay that
		 * forms part of the latency we report. */
		if (_params.dspThreads > 1)
		{
			std::vector<int> bounds;
			for (uint32_t i = 1; i < _params.dspThreads && i < 4; ++i)
				bounds.push_back(static_cast<int>(i));
			m_je8086->requestParallelPipeline(bounds, g_pipelineDelaySamples);
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

		synthLib::SysexBuffer stateBuf(_state.begin(), _state.end());
		synthLib::SysexBufferList messages;
		synthLib::MidiToSysex::splitMultipleSysex(messages, stateBuf);

		if (messages.empty())
			return false;

		m_masterVolume = -1.0f;

		for (auto& message : messages)
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

	uint32_t Device::getMaxDspThreads() const
	{
		return 4;	// H8S+ASIC0 | ASIC1 | ASIC2 | ASIC3
	}

	uint32_t Device::getInternalLatencyMidiToOutput() const
	{
		// 4.5 ms, plus the pipeline's fixed delivery delay when it is running.
		return static_cast<uint32_t>(getSamplerate() * 4.5f / 1000.0f) + pipelineDelay();
	}

	uint32_t Device::getInternalLatencyInputToOutput() const
	{
		/* Only our own delay is reported here. Whatever the audio path costs
		 * without the pipeline is unchanged and unmeasured, so it stays 0. */
		return pipelineDelay();
	}

	uint32_t Device::pipelineDelay() const
	{
		return m_je8086 && m_je8086->hasParallelPipeline() ? static_cast<uint32_t>(g_pipelineDelaySamples) : 0;
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
		/* We are on the host's audio thread here, and it is the only place we ever
		 * see how the host schedules it. The pipeline's workers mirror it one step
		 * below; without that the host's realtime thread blocks on SCHED_OTHER
		 * workers, which is priority inversion and sounds exactly like the plugin
		 * being too slow. No-op when there is no pipeline or no realtime host. */
		pipelineAdoptHostSchedule();

		m_thread->processSamples(static_cast<uint32_t>(_samples), getExtraLatencySamples(), m_midiIn, m_midiOut);
		m_midiIn.clear();

		auto& sampleBuffer = m_thread->getSampleBuffer();

		for (size_t i=0; i<_samples; ++i)
		{
			const auto s = sampleBuffer.pop_front();

			_outputs[0][i] = dspWordToFloat(s.first) * m_masterVolume;
			_outputs[1][i] = dspWordToFloat(s.second) * m_masterVolume;
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
