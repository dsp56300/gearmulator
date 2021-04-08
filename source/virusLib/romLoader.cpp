#include "romLoader.h"

#include "../dsp56300/source/dsp56kEmu/memory.h"
#include "../dsp56300/source/dsp56kEmu/dsp.h"

#include "accessVirus.h"

namespace virusLib
{
	bool RomLoader::loadFromFile(dsp56k::Memory& _memory, const char* _filename)
	{
		const AccessVirus v(_filename);

		const auto rom = v.get_dsp_program();

		const auto& bootRom = rom.bootRom;

		m_commandStream = rom.commandStream;
		
	    printf("Program BootROM size = 0x%x\n", bootRom.size);
	    printf("Program BootROM offset = 0x%x\n", bootRom.offset);
	    printf("Program BootROM len = 0x%x\n", bootRom.data.size());
	    printf("Program Commands len = 0x%x\n", rom.commandStream.size());

		// Load BootROM in DSP memory
		dsp56k::TWord idx = 0;
		for (auto it = bootRom.data.begin(); it != bootRom.data.end(); ++it++, ++idx)
			_memory.set(dsp56k::MemArea_P, bootRom.offset + idx, *it);

		// Initialize the DSP
		dsp56k::DSP dsp(_memory, this, this);
		dsp.setPC(bootRom.offset);

		while (!m_loadFinished) 
			dsp.exec();

		/* DEBUG
		for(size_t a=0; a<dsp56k::MemArea_COUNT; ++a)
		{
			char filename[128];
			sprintf(filename, "virusMem_%c.bin", dsp56k::g_memAreaNames[a]);
			FILE* hFile = fopen(filename, "wb");
			if(hFile)
			{
				for(size_t i=0; i<_memory.size(); ++i)
				{
					const dsp56k::TWord w = _memory.get(static_cast<dsp56k::EMemArea>(a), i);
//					if(w == 0)
//						continue;
					uint8_t m = (w>>16)&0xff;
					uint8_t c = (w>>8)&0xff;
					uint8_t l = (w)&0xff;
					fwrite(&m, 1, 1, hFile);
					fwrite(&c, 1, 1, hFile);
					fwrite(&l, 1, 1, hFile);
				}
				fclose(hFile);
			}
		}
		*/

		return true;
	}

	dsp56k::TWord RomLoader::read(dsp56k::TWord _addr)
	{
		switch (_addr)
		{
		case dsp56k::Essi::ESSI_PRRC:	// Port C Direction Register
			return 0;

		case dsp56k::HostIO_HSR:		// Host Status Register (HSR)
			// indicate that we have data as long as the command stream is not empty
			if(m_commandStream.empty())
				return 0;
			return (1<<dsp56k::HSR_HRDF);		// Host Receive Data Full

		case dsp56k::HostIO_HRX:
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
