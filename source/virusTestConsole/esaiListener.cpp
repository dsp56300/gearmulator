#include "esaiListener.h"

#include "dsp56kEmu/esai.h"

using namespace dsp56k;

#ifdef _DEBUG
size_t g_writeBlockSize = 8192;
#else
size_t g_writeBlockSize = 65536;
#endif

EsaiListener::EsaiListener(dsp56k::Esai& _esai, std::string _audioFilename, const uint8_t _outChannels, TCallback _callback)
	: m_esai(_esai)
    , m_outChannels(_outChannels), audioFilename(std::move(_audioFilename)), g_nextWriteSize(g_writeBlockSize)
	, m_callback(std::move(_callback))
{
	uint32_t callbackChannel;

	if (_outChannels & 4)
		callbackChannel = 3;
	else if (_outChannels & 2)
		callbackChannel = 2;
	else
		callbackChannel = 1;

	_esai.setCallback([&](Audio* _audio)
	{
		onAudioCallback(_audio);
	}, 4, callbackChannel);

	_esai.writeEmptyAudioIn(4, 2);
}

void EsaiListener::onAudioCallback(dsp56k::Audio* audio)
{
	constexpr size_t sampleCount = 4;
	constexpr size_t channelsIn = 2;
	constexpr size_t channelsOut = 6;

	TWord inputData[channelsIn][sampleCount] = { {0,0,0,0}, {0,0,0,0} };
	TWord* audioIn[channelsIn] = { inputData[0],  inputData[1] };
	TWord outputData[6][sampleCount] = { {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0} };
	TWord* audioOut[6] = {
		(m_outChannels & 1) ? outputData[0] : nullptr,
		(m_outChannels & 1) ? outputData[1] : nullptr,
		(m_outChannels & 2) ? outputData[2] : nullptr,
		(m_outChannels & 2) ? outputData[3] : nullptr,
		(m_outChannels & 4) ? outputData[4] : nullptr,
		(m_outChannels & 4) ? outputData[5] : nullptr
	};

	ctr++;
	if ((ctr & 0x1fff) == 0)
	{
		LOG("Deliver Audio");
	}

	audio->processAudioInterleaved(audioIn, audioOut, sampleCount, channelsIn, channelsOut);

	if (!audioData.capacity())
	{
		for (int c = 0; c < channelsOut; ++c)
		{
			for (int i = 0; i < sampleCount; ++i)
			{
				if (audioOut[c] && audioOut[c][i])
				{
					audioData.reserve(2048);
					break;
				}
			}
		}

		if(audioData.capacity())
		{
			LOG("Begin writing audio to file " << audioFilename);
		}
	}

	if (audioData.capacity())
	{
		for (int i = 0; i < sampleCount; ++i)
		{
			for (int c = 0; c < 6; ++c)
			{
				if(audioOut[c])
					writeWord(audioOut[c][i]);
			}
		}

		if (audioData.size() >= g_nextWriteSize)
		{
			if (writer.write(audioFilename, 24, false, 2, 12000000 / 256, audioData))
			{
				audioData.clear();
				g_nextWriteSize = g_writeBlockSize;
			}
			else
				g_nextWriteSize += g_writeBlockSize;
		}
	}

	m_callback(this, ctr);
}
void EsaiListener::writeWord(const TWord _word)
{
	const auto d = reinterpret_cast<const uint8_t*>(&_word);
	audioData.push_back(d[0]);
	audioData.push_back(d[1]);
	audioData.push_back(d[2]);
}

