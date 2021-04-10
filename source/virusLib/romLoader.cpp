#include "romLoader.h"

#include "../dsp56300/source/dsp56kEmu/memory.h"
#include "../dsp56300/source/dsp56kEmu/dsp.h"

#include "accessVirus.h"

namespace virusLib
{
	bool RomLoader::loadFromFile(dsp56k::DSP& _dsp, const char* _filename)
	{
		const AccessVirus v(_filename);

		const auto rom = v.get_dsp_program();

		const auto& bootRom = rom.bootRom;

		m_commandStream = rom.commandStream;
		
	    printf("Program BootROM size = 0x%x\n", bootRom.size);
	    printf("Program BootROM offset = 0x%x\n", bootRom.offset);
	    printf("Program BootROM len = 0x%x\n", bootRom.data.size());
	    printf("Program Commands len = 0x%x\n", rom.commandStream.size());

		auto& mem = _dsp.memory();

		_dsp.setPeriph(0, this);

		// Load BootROM in DSP memory
		dsp56k::TWord idx = 0;
		for (auto it = bootRom.data.begin(); it != bootRom.data.end(); ++it++, ++idx)
			mem.set(dsp56k::MemArea_P, bootRom.offset + idx, *it);

		// Initialize the DSP
		_dsp.setPC(bootRom.offset);

		while (!m_loadFinished) 
			_dsp.exec();

		return true;
	}

	dsp56k::TWord RomLoader::read(dsp56k::TWord _addr)
	{
		switch (_addr)
		{
		case dsp56k::Essi::ESSI_PRRC:	// Port C Direction Register
			return 0;

		case dsp56k::HI08::HSR:		// Host Status Register (HSR)
			// indicate that we have data as long as the command stream is not empty
			if(m_commandStream.empty())
				return 0;
			return (1<<dsp56k::HI08::HSR_HRDF);		// Host Receive Data Full

		case dsp56k::HI08::HRX:
			{
				const auto ret = m_commandStream.front();
				m_commandStream.erase(m_commandStream.begin());
				return ret;
			}

		case 0xFFFFC4:					// Host Polarity Control Register
			return 0;
		case 0xFFFFF5:					// ID Register
			return 0x362;
		}
		return 0;
	}

	void RomLoader::write(dsp56k::TWord _addr, dsp56k::TWord _value)
	{
	}

	void RomLoader::reset()
	{
		if(m_commandStream.empty())
			m_loadFinished = true;
	}
}
