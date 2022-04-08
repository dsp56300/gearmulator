#include "esaiListener.h"

#include "dsp56kEmu/esai.h"

using namespace dsp56k;

#ifdef _DEBUG
size_t g_writeBlockSize = 8192;
#else
size_t g_writeBlockSize = 65536;
#endif

EsaiListener::EsaiListener(dsp56k::Esai& _esai, const uint8_t _outChannels, const uint8_t _inChannels, TCallback _callback)
	: m_outChannels(_outChannels)
	, m_inChannels(_inChannels)
	, m_nextWriteSize(g_writeBlockSize)
	, m_callback(std::move(_callback))
{
	if (_outChannels & 32)		++m_outChannelCount;
	if (_outChannels & 16)		++m_outChannelCount;
	if (_outChannels & 8)		++m_outChannelCount;
	if (_outChannels & 4)		++m_outChannelCount;
	if (_outChannels & 2)		++m_outChannelCount;
	if (_outChannels & 1)		++m_outChannelCount;

	_esai.setCallback([&](Audio* _audio)
	{
		onAudioCallback(_audio);
	}, 4);

	_esai.writeEmptyAudioIn(4);
}

void EsaiListener::setMaxSamplecount(uint32_t _max)
{
	m_maxSampleCount = _max;
}

void EsaiListener::onAudioCallback(dsp56k::Audio* _audio)
{
	constexpr size_t sampleCount = 4;
	constexpr size_t channelsIn = 8;
	constexpr size_t channelsOut = 12;

	TWord inputData[channelsIn][sampleCount] = { {0,0,0,0}, {0,0,0,0},{0,0,0,0}, {0,0,0,0},{0,0,0,0}, {0,0,0,0},{0,0,0,0}, {0,0,0,0} };
	const TWord* audioIn[channelsIn] = {
		(m_inChannels & 0x01) ? inputData[0] : nullptr,
		(m_inChannels & 0x01) ? inputData[1] : nullptr,
		(m_inChannels & 0x02) ? inputData[2] : nullptr,
		(m_inChannels & 0x02) ? inputData[3] : nullptr,
		(m_inChannels & 0x04) ? inputData[4] : nullptr,
		(m_inChannels & 0x04) ? inputData[5] : nullptr,
		(m_inChannels & 0x08) ? inputData[6] : nullptr,
		(m_inChannels & 0x08) ? inputData[7] : nullptr,
	};
	TWord outputData[channelsOut][sampleCount] = { {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0} };
	TWord* audioOut[channelsOut] = {
		(m_outChannels & 0x01) ? outputData[0] : nullptr,
		(m_outChannels & 0x01) ? outputData[1] : nullptr,
		(m_outChannels & 0x02) ? outputData[2] : nullptr,
		(m_outChannels & 0x02) ? outputData[3] : nullptr,
		(m_outChannels & 0x04) ? outputData[4] : nullptr,
		(m_outChannels & 0x04) ? outputData[5] : nullptr,
		(m_outChannels & 0x08) ? outputData[6] : nullptr,
		(m_outChannels & 0x08) ? outputData[7] : nullptr,
		(m_outChannels & 0x10) ? outputData[8] : nullptr,
		(m_outChannels & 0x10) ? outputData[9] : nullptr,
		(m_outChannels & 0x20) ? outputData[10] : nullptr,
		(m_outChannels & 0x20) ? outputData[11] : nullptr
	};

	m_counter++;
	if ((m_counter & 0x1fff) == 0)
	{
		LOG("Deliver Audio");
	}

	_audio->processAudioInterleaved(audioIn, audioOut, sampleCount);

	if (limitReached())
		return;

	if (!m_audioData.capacity())
	{
		for (int c = 0; c < channelsOut; ++c)
		{
			for (int i = 0; i < sampleCount; ++i)
			{
				if (audioOut[c] && audioOut[c][i])
				{
					m_audioData.reserve(2048);
					break;
				}
			}
		}

		if(m_audioData.capacity())
			onBeginDeliverAudioData();
	}

	if (m_audioData.capacity())
	{
		for (int i = 0; i < sampleCount; ++i)
		{
			for (int c = 0; c < channelsOut; ++c)
			{
				if(audioOut[c] && !limitReached())
				{
					m_audioData.push_back(audioOut[c][i]);
					++m_processedSampleCount;
				}
			}
		}

		if (m_audioData.size() >= m_nextWriteSize || limitReached())
		{
			if (onDeliverAudioData(m_audioData))
			{
				m_audioData.clear();
				m_nextWriteSize = g_writeBlockSize;
			}
			else
				m_nextWriteSize += g_writeBlockSize;
		}
	}

	m_callback(this, m_counter);
}
