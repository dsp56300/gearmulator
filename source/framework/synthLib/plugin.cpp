#include "plugin.h"
#include "device.h"

#include <cmath>

#include "baseLib/os.h"

using namespace synthLib;

namespace synthLib
{
	constexpr uint8_t g_stateVersion = 1;

	Plugin::Plugin(Device* _device, CallbackDeviceInvalid _callbackDeviceInvalid)
	: m_resampler(_device->getChannelCountIn(), _device->getChannelCountOut())
	, m_device(_device)
	, m_midiClock(*this)
	, m_deviceSamplerate(_device->getSamplerate())
	, m_callbackDeviceInvalid(std::move(_callbackDeviceInvalid))
	{
		std::vector<float> rates;
		m_device->getDynamicSamplerates(rates);
		m_resampler.prepareDeviceSamplerates(rates);
	}

	void Plugin::addMidiEvent(const SMidiEvent& _ev)
	{
		std::lock_guard lock(m_lockAddMidiEvent);

		if(m_midiInRingBuffer.full())
		{
			std::lock_guard l(m_lock);
			processMidiInEvent(m_midiInRingBuffer.pop_front());
		}
		m_midiInRingBuffer.push_back(_ev);
	}

	bool Plugin::setPreferredDeviceSamplerate(const float _samplerate)
	{
		std::lock_guard lock(m_lock);

		const auto sr = m_device->getDeviceSamplerate(_samplerate, m_hostSamplerate);

		if(sr == m_deviceSamplerate)  // NOLINT(clang-diagnostic-float-equal)
			return true;

		if(!m_device->setSamplerate(sr))
			return false;

		m_deviceSamplerate = m_device->getSamplerate();
		m_resampler.setSamplerates(m_hostSamplerate, m_deviceSamplerate);

		updateDeviceLatency();
		return true;
	}

	void Plugin::setHostSamplerate(const float _hostSamplerate, const float _preferredDeviceSamplerate)
	{
		std::lock_guard lock(m_lock);

		m_deviceSamplerate = m_device->getDeviceSamplerate(_preferredDeviceSamplerate, _hostSamplerate);
		m_device->setSamplerate(m_deviceSamplerate);
		m_deviceSamplerate = m_device->getSamplerate();
		m_resampler.setSamplerates(_hostSamplerate, m_deviceSamplerate);

		m_hostSamplerate = _hostSamplerate;
		m_hostSamplerateInv = _hostSamplerate > 0 ? 1.0f / _hostSamplerate : 0.0f;

		updateDeviceLatency();
	}

	void Plugin::setResamplerMode(const Resampler::Mode _mode)
	{
		std::lock_guard lock(m_lock);
		m_resampler.setResamplerMode(_mode);
		updateDeviceLatency();
	}

	void Plugin::process(const TAudioInputs& _inputs, const TAudioOutputs& _outputs, size_t _count, const float _bpm,
		const float _ppqPos, const bool _isPlaying, const bool _hasPpqPosition)
	{
		baseLib::setFlushDenormalsToZero();

		TAudioInputs inputs(_inputs);
		TAudioOutputs outputs(_outputs);

		for(size_t i=0; i<inputs.size(); ++i)
			inputs[i] = _inputs[i] ? _inputs[i] : getDummyBuffer(_count);

		for(size_t i=0; i<outputs.size(); ++i)
			outputs[i] = _outputs[i] ? _outputs[i] : getDummyBuffer(_count);

		std::lock_guard lock(m_lock);

		if(!m_device->isValid())
		{
			m_device = m_callbackDeviceInvalid(m_device);

			if(!m_device || !m_device->isValid())
				return;
		}

		// Firmware can change the device clock during a patch/state change. Switching the
		// resampler over means building new filter tables and prewarming them, which must not
		// happen on this thread, so only note it down here.
		const auto deviceRate = m_device->getSamplerate();
		if(deviceRate > 0 && deviceRate != m_deviceSamplerate)
			m_pendingDeviceSamplerate.store(deviceRate, std::memory_order_relaxed);

		const auto discontinuity = updateTransport(_bpm, _ppqPos, _isPlaying, _hasPpqPosition, _count);
		if (discontinuity != TransportDiscontinuity::None)
		{
			SMidiEvent marker(MidiEventSource::Internal);
			marker.type = MidiEventType::TransportDiscontinuity;
			marker.transportGeneration = m_transportGeneration.fetch_add(1, std::memory_order_relaxed) + 1;

			// A very large host block can overflow the input ring and stage some of
			// that same block's events early. They belong to the new transport
			// generation, and the marker must still reach the device before them.
			for (auto& event : m_midiIn)
				stampTransportGeneration(event);
			m_midiIn.insert(m_midiIn.begin(), marker);
		}

		processMidiInEvents();
		processMidiClock(_bpm, _ppqPos, _isPlaying, _count);

		m_resampler.process(inputs, outputs, m_midiIn, m_midiOut, static_cast<uint32_t>(_count), 
			[&](const TAudioInputs& _ins, const TAudioOutputs& _outs, size_t _c, const ResamplerInOut::TMidiVec& _midiIn, ResamplerInOut::TMidiVec& _midiOut)
		{
			m_device->process(_ins, _outs, _c, _midiIn, _midiOut);
		});

		m_midiIn.clear();

		// The resampler grows its latency while it prewarms, which happens right here
		updateLatencies();
	}

	void Plugin::getMidiOut(std::vector<SMidiEvent>& _midiOut)
	{
		std::swap(_midiOut, m_midiOut);
		m_midiOut.clear();
	}

	bool Plugin::isValid() const
	{
		return m_device->isValid();
	}

	void Plugin::setDevice(Device* _device)
	{
		if(!_device)
			return;

		std::lock_guard lock(m_lock);

		std::vector<uint8_t> deviceState;
		getState(deviceState, StateTypeGlobal);

		delete m_device;

		m_device = _device;

		m_device->setSamplerate(m_deviceSamplerate);
		if(!deviceState.empty())
			setState(deviceState);
		m_deviceSamplerate = m_device->getSamplerate();
		std::vector<float> rates;
		m_device->getDynamicSamplerates(rates);
		m_resampler.prepareDeviceSamplerates(rates);
		m_resampler.setSamplerates(m_hostSamplerate, m_deviceSamplerate);

		// MIDI clock has to send the start event again, some device find it confusing and do strange things if there isn't any
		if (m_midiClockEnabled)
			m_midiClock.restart();

		updateDeviceLatency();
	}

#if !SYNTHLIB_DEMO_MODE
	bool Plugin::getState(std::vector<uint8_t>& _state, StateType _type) const
	{
		if(!m_device)
			return false;

		_state.push_back(g_stateVersion);
		_state.push_back(_type);

		return m_device->getState(_state, _type);
	}

	bool Plugin::setState(const std::vector<uint8_t>& _state) const
	{
		if(!m_device)
			return false;

		if(_state.empty())
			return false;

		if(_state.size() < 2)
			return m_device->setStateFromUnknownCustomData(_state);

		const auto version = _state[0];

		if(version != g_stateVersion)
			return m_device->setStateFromUnknownCustomData(_state);

		const auto stateType = static_cast<StateType>(_state[1]);

		auto state = _state;
		state.erase(state.begin(), state.begin() + 2);

		return m_device->setState(state, stateType);
	}
#endif
	void Plugin::insertMidiEvent(const SMidiEvent& _ev)
	{
		// Find the slot before inserting: stampTransportGeneration() only writes
		// transportGeneration, never offset, so the position does not depend on it. That lets the
		// event be copied into place once and stamped there, instead of copied into a local and
		// then again into the vector - this runs per clock tick on the audio thread.
		auto it = m_midiIn.end();

		if(!m_midiIn.empty() && m_midiIn.back().offset > _ev.offset)
		{
			it = m_midiIn.begin();
			while (it != m_midiIn.end() && it->offset <= _ev.offset)
				++it;
		}

		stampTransportGeneration(*m_midiIn.insert(it, _ev));
	}

	bool Plugin::setLatencyBlocks(uint32_t _latencyBlocks)
	{
		std::lock_guard lock(m_lock);

		if(m_extraLatencyBlocks == _latencyBlocks)
			return false;

		m_extraLatencyBlocks = _latencyBlocks;
		updateDeviceLatency();
		return true;
	}

	void Plugin::setMidiClockEnabled(bool _enabled)
	{
		if (m_midiClockEnabled == _enabled)
			return;

		m_midiClockEnabled = _enabled;

		if (_enabled)
			m_midiClock.restart();
	}

	void Plugin::processMidiClock(const float _bpm, const float _ppqPos, const bool _isPlaying, const size_t _sampleCount)
	{
		if (m_midiClockEnabled)
			m_midiClock.process(_bpm, _ppqPos, _isPlaying, _sampleCount);
	}

	float* Plugin::getDummyBuffer(size_t _minimumSize)
	{
		if(m_dummyBuffer.size() < _minimumSize)
			m_dummyBuffer.resize(_minimumSize);

		return m_dummyBuffer.data();
	}

	bool Plugin::applyPendingDeviceSamplerate()
	{
		const auto rate = m_pendingDeviceSamplerate.exchange(0.0f, std::memory_order_relaxed);

		if(rate <= 0.0f)
			return false;

		std::lock_guard lock(m_lock);

		if(rate == m_deviceSamplerate)
			return false;

		m_deviceSamplerate = rate;
		m_resampler.setDeviceSamplerate(rate);
		updateDeviceLatency();

		return true;
	}

	void Plugin::updateDeviceLatency()
	{
		if(m_blockSize <= 0 || m_hostSamplerate <= 0)
			return;

		const auto latency = static_cast<uint32_t>(std::ceil(static_cast<float>(m_blockSize * m_extraLatencyBlocks) * m_device->getSamplerate() * m_hostSamplerateInv));
		m_device->setExtraLatencySamples(latency);

		m_deviceLatencyMidiToOutput = static_cast<uint32_t>(static_cast<float>(m_device->getInternalLatencyMidiToOutput()) * m_hostSamplerate / m_device->getSamplerate());
		m_deviceLatencyInputToOutput = static_cast<uint32_t>(static_cast<float>(m_device->getInternalLatencyInputToOutput()) * m_hostSamplerate / m_device->getSamplerate());

		updateLatencies();
	}

	void Plugin::updateLatencies()
	{
		const auto blocks = m_blockSize * m_extraLatencyBlocks;
		const auto out = m_resampler.getOutputLatency();
		const auto in = m_resampler.getInputLatency();

		m_latencyMidiToOutput.store(blocks + m_deviceLatencyMidiToOutput + out, std::memory_order_relaxed);
		m_latencyInputToOutput.store(blocks + m_deviceLatencyInputToOutput + out + in, std::memory_order_relaxed);
	}

	void Plugin::processMidiInEvents()
	{
		while (!m_midiInRingBuffer.empty())
		{
			const auto ev = m_midiInRingBuffer.pop_front();

			processMidiInEvent(ev);
		}
	}

	void Plugin::processMidiInEvent(const SMidiEvent& _ev)
	{
		// sysex might be sent in multiple chunks. Happens if coming from hardware.
		// Nothing here needs a mutable copy: stampTransportGeneration() skips sysex entirely, so
		// copying the event up front allocated a second buffer for exactly the kind of event where
		// the stamp does nothing. Plain events are stamped in place after the push instead.
		if (!_ev.sysex.empty())
		{
			const bool isComplete = _ev.sysex.front() == M_STARTOFSYSEX && _ev.sysex.back() == M_ENDOFSYSEX;

			if (isComplete)
			{
				m_midiIn.push_back(_ev);
				return;
			}

			const bool isStart = _ev.sysex.front() == M_STARTOFSYSEX && _ev.sysex.back() != M_ENDOFSYSEX;
			const bool isEnd = _ev.sysex.front() != M_STARTOFSYSEX && _ev.sysex.back() == M_ENDOFSYSEX;

			if (isStart)
			{
				m_pendingSysexInput = _ev;
				return;
			}

			if (!m_pendingSysexInput.sysex.empty())
			{
				m_pendingSysexInput.sysex.insert(m_pendingSysexInput.sysex.end(), _ev.sysex.begin(), _ev.sysex.end());

				if (isEnd)
				{
					m_midiIn.push_back(m_pendingSysexInput);
					m_pendingSysexInput.sysex.clear();
				}
			}

			// The chunk has been folded into m_pendingSysexInput, and the whole message pushed if this
			// was its last piece. Falling through used to push the raw fragment as well, so the device
			// got headless slices of the dump on top of the reassembled message. A fragment with no
			// start before it is garbage to the firmware's parser too, so it is dropped the same way.
			return;
		}

		m_midiIn.push_back(_ev);
		stampTransportGeneration(m_midiIn.back());
	}

	void Plugin::stampTransportGeneration(SMidiEvent& _event) const
	{
		if (isTransportBound(_event))
			_event.transportGeneration = m_transportGeneration.load(std::memory_order_relaxed);
	}

	TransportDiscontinuity Plugin::updateTransport(const float _bpm, const float _ppqPos, const bool _isPlaying,
		const bool _hasPpqPosition, const size_t _sampleCount)
	{
		TransportDiscontinuity result = TransportDiscontinuity::None;
		if (m_transportInitialized)
		{
			if (_isPlaying != m_lastIsPlaying)
				result = _isPlaying ? TransportDiscontinuity::Start : TransportDiscontinuity::Stop;
			else if (_isPlaying && _hasPpqPosition && m_lastHasPpqPosition &&
				_bpm > 0.0f && m_lastBpm > 0.0f && m_hostSamplerate > 0.0f)
			{
				const auto expected = static_cast<double>(m_lastPpqPos) +
					static_cast<double>(m_lastSampleCount) * static_cast<double>(m_lastBpm) /
					(60.0 * static_cast<double>(m_hostSamplerate));
				// About 5 ms at 120 BPM: wide enough for host float/tempo jitter,
				// narrow enough to catch seeks, loop wraps and bounce restarts.
				if (std::fabs(static_cast<double>(_ppqPos) - expected) > 0.01)
					result = TransportDiscontinuity::Seek;
			}
		}

		m_transportInitialized = true;
		m_lastIsPlaying = _isPlaying;
		m_lastBpm = _bpm;
		m_lastPpqPos = _ppqPos;
		m_lastHasPpqPosition = _hasPpqPosition;
		m_lastSampleCount = _sampleCount;
		return result;
	}

	void Plugin::setBlockSize(const uint32_t _blockSize)
	{
		std::lock_guard lock(m_lock);
		m_blockSize = _blockSize;
		updateDeviceLatency();
	}

	uint32_t Plugin::getLatencyMidiToOutput() const
	{
		return m_latencyMidiToOutput.load(std::memory_order_relaxed);
	}

	uint32_t Plugin::getLatencyInputToOutput() const
	{
		return m_latencyInputToOutput.load(std::memory_order_relaxed);
	}
}
