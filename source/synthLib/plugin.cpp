#include "plugin.h"

#include "../dsp56300/source/dsp56kEmu/unittests.h"

using namespace synthLib;

namespace synthLib
{
	Plugin::Plugin(Device* _device) : m_device(_device)
	{
		m_resampler.setDeviceSamplerate(_device->getSamplerate());
	}

	void Plugin::addMidiEvent(const SMidiEvent& _ev)
	{
		std::lock_guard lock(m_lock);
		
		// sysex might be send in multiple chunks. Happens if coming from hardware
		if(!_ev.sysex.empty())
		{
			const bool isComplete = _ev.sysex.front() == M_STARTOFSYSEX && _ev.sysex.back() == M_ENDOFSYSEX;

			if(isComplete)
			{
				m_midiIn.push_back(_ev);
				return;
			}

			const bool isStart = _ev.sysex.front() == M_STARTOFSYSEX && _ev.sysex.back() != M_ENDOFSYSEX;
			const bool isEnd = _ev.sysex.front() != M_STARTOFSYSEX && _ev.sysex.back() == M_ENDOFSYSEX;

			if(isStart)
			{
				m_pendingSyexInput = _ev;
				return;
			}

			if(!m_pendingSyexInput.sysex.empty())
			{
				m_pendingSyexInput.sysex.insert(m_pendingSyexInput.sysex.end(), _ev.sysex.begin(), _ev.sysex.end());

				if(isEnd)
				{
					m_midiIn.push_back(m_pendingSyexInput);
					m_pendingSyexInput.sysex.clear();
				}
			}
			return;
		}

		m_midiIn.push_back(_ev);
	}

	void Plugin::setSamplerate(float _samplerate)
	{
		std::lock_guard lock(m_lock);
		m_resampler.setHostSamplerate(_samplerate);
	}

	void Plugin::process(float** _inputs, float** _outputs, size_t _count)
	{
		float* inputs[8] {};
		float* outputs[8] {};

		inputs[0] = _inputs && _inputs[0] ? _inputs[0] : getDummyBuffer(_count);
		inputs[1] = _inputs && _inputs[1] ? _inputs[1] : getDummyBuffer(_count);
		outputs[0] = _outputs && _outputs[0] ? _outputs[0] : getDummyBuffer(_count);
		outputs[1] = _outputs && _outputs[1] ? _outputs[1] : getDummyBuffer(_count);

		std::lock_guard lock(m_lock);
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

	float* Plugin::getDummyBuffer(size_t _minimumSize)
	{
		if(m_dummyBuffer.size() < _minimumSize)
			m_dummyBuffer.resize(_minimumSize);

		return &m_dummyBuffer[0];
	}

	void Plugin::setBlockSize(size_t _blockSize)
	{
		std::lock_guard lock(m_lock);
		m_device->setBlockSize(_blockSize);
	}
}
