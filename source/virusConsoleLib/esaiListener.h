#pragma once

#include "../synthLib/wavWriter.h"

#include "dsp56kEmu/types.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace dsp56k
{
	class Audio;
	class Esai;
}

class EsaiListener
{
public:
	using TCallback = std::function<void(EsaiListener*, uint32_t)>;

	EsaiListener(dsp56k::Esai& _esai, uint8_t _outChannels, TCallback _callback);
	virtual ~EsaiListener() = default;

	void setMaxSamplecount(uint32_t _max);

	bool limitReached() const
	{
		return m_maxSampleCount && m_processedSampleCount >= m_maxSampleCount;
	}
private:
	virtual bool onDeliverAudioData(const std::vector<dsp56k::TWord>& _audioData) = 0;
	virtual void onBeginDeliverAudioData() {};

	void onAudioCallback(dsp56k::Audio* _audio);

	const uint8_t m_outChannels;

	std::vector<dsp56k::TWord> m_audioData;
	int m_counter = 0;
	size_t m_nextWriteSize;
	const TCallback m_callback;
	uint32_t m_maxSampleCount = 0;
	uint32_t m_processedSampleCount = 0;
};
