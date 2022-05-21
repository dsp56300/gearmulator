#pragma once
#include "esaiListener.h"

class EsaiListenerToCallback : public EsaiListener
{
public:
	using TDataCallback = std::function<bool(const std::vector<dsp56k::TWord>&)>;

	EsaiListenerToCallback(dsp56k::Esai& _esai, uint8_t _outChannels, uint8_t _inChannels, TCallback _countCallback, TDataCallback _dataCallback);

private:
	bool onDeliverAudioData(const std::vector<dsp56k::TWord>& _audioData) override;

	const TDataCallback m_callback;
};
