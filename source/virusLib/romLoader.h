#pragma once
#include <vector>

#include "../dsp56300/source/dsp56kEmu/peripherals.h"

namespace dsp56k
{
	class Memory;
}

namespace virusLib
{
	class RomLoader : dsp56k::IPeripherals
	{
	public:
		bool loadFromFile(dsp56k::DSP& _dsp, const char* _filename);
	private:
		dsp56k::TWord read(dsp56k::TWord _addr) override;
		void write(dsp56k::TWord _addr, dsp56k::TWord _value) override;
		void exec() override {}
		void reset() override;

		std::vector<uint32_t> m_commandStream;
		bool m_loadFinished = false;
	};
}
