#include "xtWavePreview.h"

#include "xt.h"
#include "xtHardware.h"

namespace xt
{
	static constexpr uint32_t g_waveMemBase				= 0x20000;
	static constexpr uint32_t g_waveMemWaveSize			= 256;											// 128 + 64 + 32 + 16 + 8 + 4 + 2 + 1 + 1
	static constexpr uint32_t g_waveMemWavesPerPart		= 64;
	static constexpr uint32_t g_waveMemPartBufferSize	= g_waveMemWaveSize * g_waveMemWavesPerPart;

	WavePreview::WavePreview(Xt& _xt)
		: m_xt(_xt)
		, m_dspMem(m_xt.getHardware()->getDSP(0).dsp().memory())
	{
	}

	bool WavePreview::receiveWave(const SysEx& _data)
	{
		const auto part      = _data[SysexIndex::IdxWaveIndexL];
		const auto waveIndex = _data[SysexIndex::IdxWaveIndexH];

		if(part >= m_partDatas.size())
			return false;

		auto& partData = m_partDatas[part];

		if(waveIndex >= partData.waves.size())
			return false;

		State::parseWaveData(partData.waves[waveIndex], _data);

		for(uint8_t i=0; i<64; ++i)
			sendToDSP(partData.waves[waveIndex], 0, i);

		return true;
	}

	bool WavePreview::receiveWaveControlTable(const SysEx& _data)
	{
		return true;
	}

	bool WavePreview::receiveWavePreviewMode(const SysEx& _data)
	{
		return true;
	}

	void WavePreview::sendToDSP(const WaveData& _data, const uint8_t _part, const uint8_t _wave)
	{
		const auto memBase = g_waveMemBase + _part * g_waveMemPartBufferSize + _wave * g_waveMemWaveSize;

		std::array<int8_t, g_waveMemWaveSize> waveData;

		waveData.back() = 0;

		std::copy(_data.begin(), _data.end(), waveData.begin());

		// super simple repeating factor-two reduction by averaging two samples. No idea how the hardware does it
		uint32_t size = static_cast<uint32_t>(_data.size()) >> 1;
		uint32_t readOffset = 0;
		uint32_t writeOffset = size<<1;
		while(size)
		{
			for(uint32_t i=0; i<size; ++i)
			{
				const auto s = (waveData[readOffset+(i<<1)] + waveData[readOffset+(i<<1)+1]) >> 1;
				waveData[writeOffset+i] = static_cast<int8_t>(s);
			}
			readOffset += size<<1;
			writeOffset += size;
			size >>= 1;
		}

		waveData[waveData.size()-1] = waveData[waveData.size()-2] = 0;

		for(uint32_t i=0; i<waveData.size(); ++i)
		{
			m_dspMem.set(dsp56k::MemArea_Y, memBase + i, static_cast<int32_t>(waveData[i]) << 16);
		}
	}
}
