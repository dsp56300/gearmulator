#include "plugin.h"
#include "device.h"

#include <cmath>

#include "os.h"

using namespace synthLib;

namespace synthLib
{
	constexpr uint8_t g_stateVersion = 1;

	Plugin::Plugin(Device* _device)
	: m_resampler(_device->getChannelCountIn(), _device->getChannelCountOut())
	, m_device(_device)
	, m_deviceSamplerate(_device->getSamplerate())
	, m_midiClock(*this)
	{
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

		if(sr == m_deviceSamplerate)
			return true;

		if(!m_device->setSamplerate(sr))
			return false;

		m_deviceSamplerate = sr;
		m_resampler.setSamplerates(m_hostSamplerate, m_deviceSamplerate);

		updateDeviceLatency();
		return true;
	}

	void Plugin::setHostSamplerate(const float _samplerate, float _preferredDeviceSamplerate)
	{
		std::lock_guard lock(m_lock);

		m_deviceSamplerate = m_device->getDeviceSamplerate(_preferredDeviceSamplerate, _samplerate);
		m_device->setSamplerate(m_deviceSamplerate);
		m_resampler.setSamplerates(_samplerate, m_deviceSamplerate);

		m_hostSamplerate = _samplerate;
		m_hostSamplerateInv = _samplerate > 0 ? 1.0f / _samplerate : 0.0f;

		updateDeviceLatency();
	}

	void Plugin::process(const TAudioInputs& _inputs, const TAudioOutputs& _outputs, size_t _count, const float _bpm, const float _ppqPos, const bool _isPlaying)
	{
		setFlushDenormalsToZero();

		TAudioInputs inputs(_inputs);
		TAudioOutputs outputs(_outputs);

		for(size_t i=0; i<inputs.size(); ++i)
			inputs[i] = _inputs[i] ? _inputs[i] : getDummyBuffer(_count);

		for(size_t i=0; i<outputs.size(); ++i)
			outputs[i] = _outputs[i] ? _outputs[i] : getDummyBuffer(_count);

		std::lock_guard lock(m_lock);

		if(!m_device->isValid())
			return;

		processMidiInEvents();
		processMidiClock(_bpm, _ppqPos, _isPlaying, _count);

		m_resampler.process(inputs, outputs, m_midiIn, m_midiOut, static_cast<uint32_t>(_count), 
			[&](const TAudioInputs& _ins, const TAudioOutputs& _outs, size_t _c, const ResamplerInOut::TMidiVec& _midiIn, ResamplerInOut::TMidiVec& _midiOut)
		{
			m_device->process(_ins, _outs, _c, _midiIn, _midiOut);
		});

		m_midiIn.clear();
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
		setState(deviceState);

		// MIDI clock has to send the start event again, some device find it confusing and do strange things if there isn't any
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

	bool Plugin::setState(const std::vector<uint8_t>& _state)
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
		if(m_midiIn.empty() || m_midiIn.back().offset <= _ev.offset)
		{
			m_midiIn.push_back(_ev);
			return;
		}

		for (auto it = m_midiIn.begin(); it != m_midiIn.end(); ++it)
		{
			if (it->offset > _ev.offset)
			{
				m_midiIn.insert(it, _ev);
				return;
			}
		}

		m_midiIn.push_back(_ev);
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

	void Plugin::processMidiClock(const float _bpm, const float _ppqPos, const bool _isPlaying, const size_t _sampleCount)
	{
		m_midiClock.process(_bpm, _ppqPos, _isPlaying, _sampleCount);
	}

	float* Plugin::getDummyBuffer(size_t _minimumSize)
	{
		if(m_dummyBuffer.size() < _minimumSize)
			m_dummyBuffer.resize(_minimumSize);

		return m_dummyBuffer.data();
	}

	void Plugin::updateDeviceLatency()
	{
		if(m_blockSize <= 0 || m_hostSamplerate <= 0)
			return;

		const auto latency = static_cast<uint32_t>(std::ceil(static_cast<float>(m_blockSize * m_extraLatencyBlocks) * m_device->getSamplerate() * m_hostSamplerateInv));
		m_device->setExtraLatencySamples(latency);

		m_deviceLatencyMidiToOutput = static_cast<uint32_t>(static_cast<float>(m_device->getInternalLatencyMidiToOutput()) * m_hostSamplerate / m_device->getSamplerate());
		m_deviceLatencyInputToOutput = static_cast<uint32_t>(static_cast<float>(m_device->getInternalLatencyInputToOutput()) * m_hostSamplerate / m_device->getSamplerate());
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
		// sysex might be sent in multiple chunks. Happens if coming from hardware
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
		}

		m_midiIn.push_back(_ev);
	}

	void Plugin::setBlockSize(const uint32_t _blockSize)
	{
		std::lock_guard lock(m_lock);
		m_blockSize = _blockSize;
		updateDeviceLatency();
	}

	uint32_t Plugin::getLatencyMidiToOutput() const
	{
		std::lock_guard lock(m_lock);
		return m_blockSize * m_extraLatencyBlocks + m_deviceLatencyMidiToOutput + m_resampler.getOutputLatency();
	}

	uint32_t Plugin::getLatencyInputToOutput() const
	{
		std::lock_guard lock(m_lock);
		return m_blockSize * m_extraLatencyBlocks + m_deviceLatencyInputToOutput + m_resampler.getOutputLatency() + m_resampler.getInputLatency();
	}
}
