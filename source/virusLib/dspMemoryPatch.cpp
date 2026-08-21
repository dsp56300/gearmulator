#include "dspMemoryPatch.h"

#include "dsp56kEmu/dsp.h"

namespace virusLib
{
	std::string DspMemoryPatch::toString() const
	{
		std::stringstream ss;
		if(area >= dsp56k::MemArea_COUNT)
			ss << '?';
		else
			ss << dsp56k::g_memAreaNames[area];

		ss << ':' << HEX(address) << '=' << HEX(newValue);

		if(newValue != expectedOldValue)
			ss << ", expected old = " << HEX(expectedOldValue);
		return ss.str();
	}

	bool DspMemoryPatchSet::apply(dsp56k::DSP& _dsp, const std::initializer_list<DspMemoryPatch>& _patches)
	{
		bool res = true;

		for (const auto& patch : _patches)
			res &= apply(_dsp, patch);

		return res;
	}

	bool DspMemoryPatchSet::apply(dsp56k::DSP& _dsp, const DspMemoryPatch& _patch)
	{
		auto& mem = _dsp.memory();

		if(_patch.area == dsp56k::MemArea_COUNT)
		{
			LOG("Failed to apply patch, memory area is not valid, for patch " << _patch.toString());
			return false;
		}

		if(_patch.address >= mem.size(_patch.area))
		{
			LOG("Failed to apply patch, address " << HEX(_patch.address) << " is out of range, area " << dsp56k::g_memAreaNames[_patch.area] << " size is " << HEX(mem.size(_patch.area)) << ", for patch " << _patch.toString());
			return false;
		}

		if(_patch.expectedOldValue != _patch.newValue)
		{
			const auto v = mem.get(_patch.area, _patch.address);
			if(v != _patch.expectedOldValue)
			{
				LOG("Failed to apply patch, expected current value to be " << HEX(_patch.expectedOldValue) << " but current value is " << HEX(v) << ", for patch " << _patch.toString());
				return false;
			}
		}

		if(_patch.area == dsp56k::MemArea_P)
			_dsp.memWriteP(_patch.address, _patch.newValue);
		else
			_dsp.memWrite(_patch.area, _patch.address, _patch.newValue);

		LOG("Successfully applied patch " << _patch.toString());

		return true;
	}

	bool DspMemoryPatchSet::apply(dsp56k::DSP& _dsp, const baseLib::MD5& _md5) const
	{
		// A default-constructed MD5 means "nobody computed this" and must not authorise a
		// patch set, in either position. `static const`, not `static constexpr`: MD5's
		// default constructor is not constexpr (it assigns m_h in the body).
		static const baseLib::MD5 g_unknown{};

		if(_md5 == g_unknown)
			return false;

		for (const auto& e : allowedTargets)
		{
			if(e == g_unknown)
				continue;
			if(e == _md5)
				return apply(_dsp, patches);
		}
		return false;
	}
}
