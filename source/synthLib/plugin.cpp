#include "plugin.h"
#include "device.h"

#include <cmath>

#include "os.h"

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
			processMidiInEvents();
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

	void Plugin::process(float** _inputs, float** _outputs, size_t _count, const float _bpm, const float _ppqPos, const bool _isPlaying)
	{
		if(!m_device->isValid())
			return;

		setFlushDenormalsToZero();

		processMidiInEvents();

		float* inputs[8] {};
		float* outputs[8] {};

		inputs[0] = _inputs && _inputs[0] ? _inputs[0] : getDummyBuffer(_count);
		inputs[1] = _inputs && _inputs[1] ? _inputs[1] : getDummyBuffer(_count);
		outputs[0] = _outputs && _outputs[0] ? _outputs[0] : getDummyBuffer(_count);
		outputs[1] = _outputs && _outputs[1] ? _outputs[1] : getDummyBuffer(_count);

		std::lock_guard lock(m_lock);

		processMidiClock(_bpm, _ppqPos, _isPlaying, _count);

		m_resampler.process(inputs, outputs, m_midiIn, m_midiOut, static_cast<uint32_t>(_count), 
			[&](float** _in, float** _out, size_t _c, const ResamplerInOut::TMidiVec& _midiIn, ResamplerInOut::TMidiVec& _midiOut)
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

	bool Plugin::getState(std::vector<uint8_t>& _state, StateType _type)
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

	void Plugin::processMidiClock(float _bpm, float _ppqPos, bool _isPlaying, size_t _sampleCount)
	{
		if(_bpm < 1.0f)
			return;

		auto needsStart = false;

		if(_isPlaying && !m_isPlaying)
		{
			const uint32_t beat = dsp56k::floor_int(_ppqPos);

			if(beat > m_lastKnownBeat)
			{
				// start
				m_isPlaying = true;
				needsStart = true;
			}
			m_lastKnownBeat = beat;
		}
		else if(m_isPlaying && !_isPlaying)
		{
			m_isPlaying = false;

			SMidiEvent evStop;
			evStop.a = M_STOP;
			m_midiIn.insert(m_midiIn.begin(), evStop);
			m_clockTickPos = 0.0f;
		}

		if(m_isPlaying)
		{
			constexpr float clockTicksPerQuarter = 24.0f;

			const float quartersPerSecond = _bpm / 60.0f;
			const float clockTicksPerSecond = clockTicksPerQuarter * quartersPerSecond;
			const float samplesPerClock = m_hostSamplerate / clockTicksPerSecond;

			const auto clockTickPos = _ppqPos * clockTicksPerQuarter;

			if(m_clockTickPos <= 0.001f || clockTickPos < m_clockTickPos)
				m_clockTickPos = std::floor(clockTickPos);

			const float offset = clockTickPos - m_clockTickPos;

			const float firstSamplePos = std::max((1.0f - offset) * samplesPerClock, 0.0f);

			const auto max = static_cast<float>(_sampleCount);

			SMidiEvent evClock;

			const auto midiEventsEmpty = m_midiIn.empty();

			for(float pos = std::floor(firstSamplePos); pos < max; pos += samplesPerClock)
			{
				const int insertPos = dsp56k::floor_int(pos);

				bool found = false;

				if(!midiEventsEmpty)
				{
					for(auto it = m_midiIn.begin(); it != m_midiIn.end(); ++it)
					{
						if(static_cast<int>(it->offset) > insertPos)
						{
							evClock.a = needsStart ? M_START : M_TIMINGCLOCK;
							evClock.offset = insertPos;
							m_midiIn.insert(it, evClock);
							found = true;
							break;
						}
					}
				}

				if(midiEventsEmpty || !found)
				{
					evClock.a = needsStart ? M_START : M_TIMINGCLOCK;
					evClock.offset = insertPos;
					m_midiIn.push_back(evClock);
				}

				needsStart = false;
				m_clockTickPos += 1.0f;
			}
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

		const auto latency = static_cast<uint32_t>(std::ceil(static_cast<float>(m_blockSize) * m_device->getSamplerate() * m_hostSamplerateInv));
		m_device->setLatencySamples(latency);

		m_deviceLatency = static_cast<uint32_t>(m_device->getInternalLatencySamples() * m_hostSamplerate / m_device->getSamplerate());
	}

	void Plugin::processMidiInEvents()
	{
		while (!m_midiInRingBuffer.empty())
		{
			const auto _ev = m_midiInRingBuffer.pop_front();

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
				return;
			}

			m_midiIn.push_back(_ev);
		}
	}

	void Plugin::setBlockSize(const uint32_t _blockSize)
	{
		std::lock_guard lock(m_lock);
		m_blockSize = _blockSize;
		updateDeviceLatency();
	}

	uint32_t Plugin::getLatencySamples() const
	{
		std::lock_guard lock(m_lock);
		return m_blockSize + m_deviceLatency;
	}
}
