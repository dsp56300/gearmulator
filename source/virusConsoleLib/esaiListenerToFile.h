#pragma once
#include <array>

#include "esaiListener.h"

class EsaiListenerToFile : public EsaiListener
{
public:
	EsaiListenerToFile(dsp56k::Esai& _esai, uint8_t _outChannels, uint8_t _inChannels, TCallback _callback, uint32_t _samplerate, std::string _audioFilename);

	static void writeWord(std::vector<uint8_t>& _dst, dsp56k::TWord _word);

private:
	bool onDeliverAudioData(const std::vector<dsp56k::TWord>& _audioData) override;
	void onBeginDeliverAudioData() override;

	void writeWord(uint8_t _channel, dsp56k::TWord _word);

	std::array<synthLib::WavWriter,3> m_writers;
	const uint32_t m_samplerate;
	const std::string m_audioFilename;
	std::vector<uint8_t> m_audioData;
	std::array<std::string,3> m_audioFilenames;
	std::array<std::vector<uint8_t>,3> m_audioDatas;
};
