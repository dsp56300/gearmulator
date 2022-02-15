#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "../synthLib/wavWriter.h"
#include "dsp56kEmu/types.h"

namespace dsp56k
{
	class Audio;
	class Esai;
}

class EsaiListener
{
public:
	using TCallback = std::function<void(EsaiListener* _listener, uint32_t)>;

	EsaiListener(dsp56k::Esai& _esai, std::string _audioFilename, uint8_t _outChannels, TCallback _callback, uint32_t _samplerate);

private:
	void onAudioCallback(dsp56k::Audio* _audio);
	void writeWord(dsp56k::TWord _word);

	dsp56k::Esai& m_esai;
	const uint8_t m_outChannels;

	std::vector<uint8_t> m_audioData;
	std::string m_audioFilename;
	synthLib::WavWriter m_writer;
	int m_counter = 0;
	size_t m_nextWriteSize;
	const TCallback m_callback;
	const uint32_t m_samplerate;
};
