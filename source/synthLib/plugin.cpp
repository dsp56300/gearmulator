#include "plugin.h"
#include "device.h"

#include <cmath>

#include "os.h"

#if 0
#define LOGMC(S)	LOG(S)
#else
#define LOGMC(S)	{}
#endif

using namespace synthLib;

namespace synthLib
{
	constexpr uint8_t g_stateVersion = 1;

	Plugin::Plugin(Device* _device) : m_device(_device)
	{
		m_resampler.setDeviceSamplerate(_device->getSamplerate());
	}

	void Plugin::addMidiEvent(const SMidiEvent& _ev)
	{
		std::lock_guard lock(m_lockAddMidiEvent);

		if(m_midiInRingBuffer.full())
		{
			std::lock_guard lock(m_lock);
			processMidiInEvent(m_midiInRingBuffer.pop_front());
		}
		m_midiInRingBuffer.push_back(_ev);
	}

	void Plugin::setSamplerate(float _samplerate)
	{
		std::lock_guard lock(m_lock);
		m_resampler.setHostSamplerate(_samplerate);
		m_hostSamplerate = _samplerate;
		m_hostSamplerateInv = _samplerate > 0 ? 1.0f / _samplerate : 0.0f;
		updateDeviceLatency();
	}

	void Plugin::process(const float** _inputs, float** _outputs, size_t _count, const float _bpm, const float _ppqPos, const bool _isPlaying)
	{
		if(!m_device->isValid())
			return;

		setFlushDenormalsToZero();


		const float* inputs[8] {};
		float* outputs[8] {};

		inputs[0] = _inputs && _inputs[0] ? _inputs[0] : getDummyBuffer(_count);
		inputs[1] = _inputs && _inputs[1] ? _inputs[1] : getDummyBuffer(_count);
		outputs[0] = _outputs && _outputs[0] ? _outputs[0] : getDummyBuffer(_count);
		outputs[1] = _outputs && _outputs[1] ? _outputs[1] : getDummyBuffer(_count);

		std::lock_guard lock(m_lock);

		processMidiInEvents();
		processMidiClock(_bpm, _ppqPos, _isPlaying, _count);

		m_resampler.process(inputs, outputs, m_midiIn, m_midiOut, static_cast<uint32_t>(_count), 
			[&](const float** _in, float** _out, size_t _c, const ResamplerInOut::TMidiVec& _midiIn, ResamplerInOut::TMidiVec& _midiOut)
		{
			m_device->process(_in, _out, _c, _midiIn, _midiOut);
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

		if(_state.size() < 2)
			return false;

		const auto version = _state[0];

		if(version != g_stateVersion)
			return false;

		const auto stateType = static_cast<StateType>(_state[1]);

		auto state = _state;
		state.erase(state.begin(), state.begin() + 2);

		return m_device->setState(state, stateType);
	}

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

	void Plugin::processMidiClock(float _bpm, float _ppqPos, bool _isPlaying, size_t _sampleCount)
	{
		if(_bpm < 1.0f)
			return;

		const double ppqPos = _ppqPos;

		constexpr double clockTicksPerQuarter = 24.0;

		if(_isPlaying && !m_isPlaying)
		{
			m_clockTickPos = (ppqPos - std::floor(ppqPos + 1.0)) * clockTicksPerQuarter;
			LOGMC("Start at ppqPos=" << ppqPos << ", clock tick offset " << m_clockTickPos);
			m_isPlaying = true;
			m_needsStart = true;
		}
		else if(m_isPlaying && !_isPlaying)
		{
			LOGMC("Stop at ppqPos=" << ppqPos);

			m_isPlaying = false;

			SMidiEvent evStop;
			evStop.a = M_STOP;
			m_midiIn.insert(m_midiIn.begin(), evStop);
			m_clockTickPos = 0.0;
		}

		if(!m_isPlaying)
			return;

		const double quartersPerSecond = _bpm / 60.0;
		const double clockTicksPerSecond = clockTicksPerQuarter * quartersPerSecond;

		const double clocksPerSample = clockTicksPerSecond * m_hostSamplerateInv;

		for(uint32_t i=0; i<static_cast<uint32_t>(_sampleCount); ++i)
		{
			m_clockTickPos += clocksPerSample;

			if (m_clockTickPos < 0.0f)
				continue;

			m_clockTickPos -= 1.0;

			LOGMC("insert tick at " << i);

			SMidiEvent evClock;
			evClock.a = M_TIMINGCLOCK;
			evClock.offset = i;

			if(m_needsStart)
			{
				evClock.a = M_START;
				insertMidiEvent(evClock);
				evClock.a = M_TIMINGCLOCK;

				m_needsStart = false;
			}

			insertMidiEvent(evClock);
		}
	}

	float* Plugin::getDummyBuffer(size_t _minimumSize)
	{
		if(m_dummyBuffer.size() < _minimumSize)
			m_dummyBuffer.resize(_minimumSize);

		return &m_dummyBuffer[0];
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
		// sysex might be send in multiple chunks. Happens if coming from hardware
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
