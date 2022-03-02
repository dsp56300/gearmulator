#include "esaiListener.h"

#include "dsp56kEmu/esai.h"

using namespace dsp56k;

#ifdef _DEBUG
size_t g_writeBlockSize = 8192;
#else
size_t g_writeBlockSize = 65536;
#endif

EsaiListener::EsaiListener(dsp56k::Esai& _esai, std::string _audioFilename, const uint8_t _outChannels, TCallback _callback, uint32_t _samplerate)
	: m_outChannels(_outChannels), m_audioFilename(std::move(_audioFilename)), m_nextWriteSize(g_writeBlockSize)
	, m_callback(std::move(_callback))
	, m_samplerate(_samplerate)
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

void EsaiListener::onAudioCallback(dsp56k::Audio* _audio)
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

	m_counter++;
	if ((m_counter & 0x1fff) == 0)
	{
		LOG("Deliver Audio");
	}

	_audio->processAudioInterleaved(audioIn, audioOut, sampleCount, channelsIn, channelsOut);

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
		{
			LOG("Begin writing audio to file " << m_audioFilename);
		}
	}

	if (m_audioData.capacity() && !m_audioFilename.empty())
	{
		for (int i = 0; i < sampleCount; ++i)
		{
			for (int c = 0; c < 6; ++c)
			{
				if(audioOut[c])
					writeWord(audioOut[c][i]);
			}
		}

		if (m_audioData.size() >= m_nextWriteSize)
		{
			if (m_writer.write(m_audioFilename, 24, false, 2, m_samplerate, m_audioData))
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
void EsaiListener::writeWord(const TWord _word)
{
	const auto d = reinterpret_cast<const uint8_t*>(&_word);
	m_audioData.push_back(d[0]);
	m_audioData.push_back(d[1]);
	m_audioData.push_back(d[2]);
}

