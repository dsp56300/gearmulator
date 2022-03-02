#include "esaiListenerToFile.h"

#include "dsp56kEmu/logging.h"

EsaiListenerToFile::EsaiListenerToFile(dsp56k::Esai& _esai, uint8_t _outChannels, TCallback _callback,	uint32_t _samplerate, std::string _audioFilename)
	: EsaiListener(_esai, _outChannels, std::move(_callback))
	, m_samplerate(_samplerate)
	, m_audioFilename(std::move(_audioFilename))
{
	m_audioData.reserve(65536);
}

bool EsaiListenerToFile::onDeliverAudioData(const std::vector<dsp56k::TWord>& _audioData)
{
	if (m_audioFilename.empty())
		return true;

	for (const auto& data : _audioData)
	{
		writeWord(data);
	}
	const auto res = m_writer.write(m_audioFilename, 24, false, 2, m_samplerate, m_audioData);
	m_audioData.clear();
	return res;
}

void EsaiListenerToFile::onBeginDeliverAudioData()
{
	LOG("Begin writing audio to file " << m_audioFilename);
}
void EsaiListenerToFile::writeWord(const dsp56k::TWord _word)
{
	const auto d = reinterpret_cast<const uint8_t*>(&_word);
	m_audioData.push_back(d[0]);
	m_audioData.push_back(d[1]);
	m_audioData.push_back(d[2]);
}

