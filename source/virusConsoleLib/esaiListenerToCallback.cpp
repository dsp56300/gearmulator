#include "esaiListenerToCallback.h"

EsaiListenerToCallback::EsaiListenerToCallback(dsp56k::Esai& _esai, uint8_t _outChannels, TCallback _countCallback, TDataCallback _dataCallback)
: EsaiListener(_esai, _outChannels, std::move(_countCallback))
, m_callback(std::move(_dataCallback))
{
}

bool EsaiListenerToCallback::onDeliverAudioData(const std::vector<dsp56k::TWord>& _audioData)
{
	return m_callback(_audioData);
}
