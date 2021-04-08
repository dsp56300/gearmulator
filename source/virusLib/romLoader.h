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
		bool loadFromFile(dsp56k::Memory& _memory, const char* _filename);
	private:
		void setDSP(dsp56k::DSP* _dsp) override {m_dsp = _dsp;}
		bool isValidAddress(dsp56k::TWord _addr ) const override { return true; }
		dsp56k::TWord read(dsp56k::TWord _addr) override;
		void write(dsp56k::TWord _addr, dsp56k::TWord _value) override;
		virtual void exec() {}
		void reset() override;
		dsp56k::DSP& getDSP() override { return *m_dsp; }

		std::vector<uint32_t> m_commandStream;
		dsp56k::DSP* m_dsp = nullptr;
		bool m_loadFinished = false;
	};
}
