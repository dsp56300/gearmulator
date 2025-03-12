#pragma once

#include <cstdint>
#include <vector>

#include "xtId.h"
#include "xtTypes.h"
#include "xtUc.h"

namespace xt
{
	using SysEx = std::vector<uint8_t>;

	class RomWaves
	{
	public:
		RomWaves(XtUc& _uc) : m_uc(_uc) {}
		bool receiveSysEx(std::vector<SysEx>& _results, const SysEx& _data) const;

	private:
		bool writeToRom(WaveId _id, const WaveData& _waveData) const;
		bool readFromRom(WaveData& _waveData, WaveId _id) const;

		XtUc& m_uc;
	};
}
