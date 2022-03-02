#pragma once
#include "esaiListener.h"

class EsaiListenerToCallback : public EsaiListener
{
public:
	using TCallback = std::function<bool(const std::vector<dsp56k::TWord>&)>;

	EsaiListenerToCallback(dsp56k::Esai& _esai, uint8_t _outChannels, TCallback _callback);

private:
	bool onDeliverAudioData(const std::vector<dsp56k::TWord>& _audioData) override;

	const TCallback m_callback;
};
