#include "plugin.h"

#include "../dsp56300/source/dsp56kEmu/unittests.h"

namespace virusLib
{
	Plugin::Plugin()
	{
		dsp56k::UnitTests tests;

		m_device.reset(new Device("c:\\Virus_C_OS_Flash_V6_5.BIN"));
	}

	Plugin::~Plugin()
	{
		m_device.reset();
	}

	void Plugin::addMidiEvent(const SMidiEvent& _ev)
	{
		m_midiIn.push_back(_ev);
	}

	void Plugin::process(float** _inputs, float** _outputs, size_t _count)
	{
		m_device->process(_inputs, _outputs, _count, m_midiIn, m_midiOut);
		m_midiIn.clear();
		m_midiOut.clear();	// TODO
	}

	void Plugin::setBlockSize(size_t _blockSize)
	{
		m_device->setBlockSize(_blockSize);
	}
}
