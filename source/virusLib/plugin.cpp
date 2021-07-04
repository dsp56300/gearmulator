#include "plugin.h"

#include "../dsp56300/source/dsp56kEmu/unittests.h"

namespace virusLib
{
	Plugin::Plugin()
	{
		m_device.reset(new Device("c:\\AccessVirusB(am29f040b_4v9).BIN"));
		m_resampler.setDeviceSamplerate(12000000.0f / 256.0f);
	}

	Plugin::~Plugin()
	{
		m_device.reset();
	}

	void Plugin::addMidiEvent(const SMidiEvent& _ev)
	{
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
		m_resampler.setHostSamplerate(_samplerate);
	}

	void Plugin::process(float** _inputs, float** _outputs, size_t _count)
	{
		m_resampler.process(_inputs, _outputs, m_midiIn, m_midiOut, static_cast<uint32_t>(_count), 
			[&](float** _in, float** _out, size_t _c, const ResamplerInOut::TMidiVec& _midiIn, ResamplerInOut::TMidiVec& _midiOut)
		{
			m_device->process(_in, _out, _c, _midiIn, _midiOut);
		});

		m_midiIn.clear();
		m_midiOut.clear();	// TODO
	}

	void Plugin::setBlockSize(size_t _blockSize)
	{
		m_device->setBlockSize(_blockSize);
	}
}
