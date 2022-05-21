#include "esaiListenerToCallback.h"

EsaiListenerToCallback::EsaiListenerToCallback(dsp56k::Esai& _esai, uint8_t _outChannels, uint8_t _inChannels, TCallback _countCallback, TDataCallback _dataCallback)
: EsaiListener(_esai, _outChannels, _inChannels, std::move(_countCallback))
, m_callback(std::move(_dataCallback))
{
}

bool EsaiListenerToCallback::onDeliverAudioData(const std::vector<dsp56k::TWord>& _audioData)
{
	return m_callback(_audioData);
}
