#include "esaiListenerToFile.h"

#include "dsp56kEmu/logging.h"

EsaiListenerToFile::EsaiListenerToFile(dsp56k::Esai& _esai, uint8_t _outChannels, uint8_t _inChannels, TCallback _callback,	uint32_t _samplerate, std::string _audioFilename)
	: EsaiListener(_esai, _outChannels, _inChannels, std::move(_callback))
	, m_samplerate(_samplerate)
	, m_audioFilename(std::move(_audioFilename))
{
	for(size_t i=0; i<m_audioFilenames.size(); ++i)
	{
		m_audioFilenames[i] = m_audioFilename;
		if(i)
			m_audioFilenames[i] = static_cast<char>('0' + i) + m_audioFilenames[i];

		m_audioDatas[i].reserve(65536);
	}
}

bool EsaiListenerToFile::onDeliverAudioData(const std::vector<dsp56k::TWord>& _audioData)
{
	if (m_audioFilename.empty())
		return true;

	if(getChannelCount() == 1)
	{
		for(size_t s=0; s<_audioData.size(); ++s)
			writeWord(0, _audioData[s]);
	}
	else
	{
		for(size_t s=0; s<_audioData.size();)
		{
			for(uint8_t c=0; c<getChannelCount(); ++c)
			{
				for(size_t i=0; i<2; ++i, ++s)
				{
					writeWord(c, _audioData[s]);
				}
			}
		}
	}

	for(size_t i=0; i<getChannelCount(); ++i)
	{
		if(m_writers[i].write(m_audioFilenames[i], 24, false, 2, m_samplerate, m_audioDatas[i]))
			m_audioDatas[i].clear();
	}
	return true;
}

void EsaiListenerToFile::onBeginDeliverAudioData()
{
	LOG("Begin writing audio to file " << m_audioFilename);
}

void EsaiListenerToFile::writeWord(const uint8_t _channel, const dsp56k::TWord _word)
{
	writeWord(m_audioDatas[_channel], _word);
}

void EsaiListenerToFile::writeWord(std::vector<uint8_t>& _dst, dsp56k::TWord _word)
{
	const auto d = reinterpret_cast<const uint8_t*>(&_word);
	_dst.push_back(d[0]);
	_dst.push_back(d[1]);
	_dst.push_back(d[2]);
}
