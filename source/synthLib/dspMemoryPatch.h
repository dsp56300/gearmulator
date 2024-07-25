#pragma once

#include "baseLib/md5.h"

#include "dsp56kEmu/types.h"

namespace dsp56k
{
	class DSP;
}

namespace synthLib
{
	namespace dspOpcodes
	{
		static constexpr dsp56k::TWord g_nop  = 0x000000;
		static constexpr dsp56k::TWord g_wait = 0x000086;
	}

	struct DspMemoryPatch
	{
		dsp56k::EMemArea area = dsp56k::MemArea_COUNT;
		dsp56k::TWord address = 0;
		dsp56k::TWord expectedOldValue = 0;
		dsp56k::TWord newValue = 0;

		constexpr bool operator == (const DspMemoryPatch& _p) const
		{
			return area == _p.area && address == _p.address && expectedOldValue == _p.expectedOldValue && newValue == _p.newValue;
		}

		constexpr bool operator != (const DspMemoryPatch& _p) const
		{
			return !(*this == _p);
		}

		constexpr bool operator < (const DspMemoryPatch& _p) const
		{
			if(area < _p.area)							return true;
			if(area > _p.area)							return false;
			if(address < _p.address)					return true;
			if(address > _p.address)					return false;
			if(expectedOldValue < _p.expectedOldValue)	return true;
			if(expectedOldValue > _p.expectedOldValue)	return false;
			if(newValue < _p.newValue)					return true;
			/*if(newValue > _p.newValue)*/				return false;
		}

		std::string toString() const;
	};

	struct DspMemoryPatches
	{
		std::initializer_list<baseLib::MD5> allowedTargets;
		std::initializer_list<DspMemoryPatch> patches;

		bool apply(dsp56k::DSP& _dsp, const baseLib::MD5& _md5) const;

	private:
		static bool apply(dsp56k::DSP& _dsp, const std::initializer_list<DspMemoryPatch>& _patches);
		static bool apply(dsp56k::DSP& _dsp, const DspMemoryPatch& _patch);
	};
}
