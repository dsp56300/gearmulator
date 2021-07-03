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
