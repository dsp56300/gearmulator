#pragma once
#include "esaiListener.h"

class EsaiListenerToFile : public EsaiListener
{
public:
	EsaiListenerToFile(dsp56k::Esai& _esai, uint8_t _outChannels, TCallback _callback, uint32_t _samplerate, std::string _audioFilename);
private:
	bool onDeliverAudioData(const std::vector<dsp56k::TWord>& _audioData) override;
	void onBeginDeliverAudioData() override;

	void writeWord(dsp56k::TWord _word);

	synthLib::WavWriter m_writer;
	const uint32_t m_samplerate;
	const std::string m_audioFilename;
	std::vector<uint8_t> m_audioData;
};
