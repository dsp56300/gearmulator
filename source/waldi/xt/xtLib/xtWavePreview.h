#pragma once

#include "xtState.h"

namespace dsp56k
{
	class Memory;
}

namespace xt
{
	class Xt;

	class WavePreview
	{
	public:
		struct PartData
		{
			std::array<WaveData, 64> waves{};
			std::array<uint16_t, 64> control{};
		};

		WavePreview(Xt& _xt);

		bool receiveWave(const SysEx& _data);
		bool receiveWaveControlTable(const SysEx& _data);
		bool receiveWavePreviewMode(const SysEx& _data);

	private:
		void sendToDSP(const WaveData& _data, uint8_t _part, uint8_t _wave);

		Xt& m_xt;
		dsp56k::Memory& m_dspMem;
		std::array<PartData, 8> m_partDatas;
	};
}
