#include "esaiListenerToCallback.h"

EsaiListenerToCallback::EsaiListenerToCallback(dsp56k::Esai& _esai, uint8_t _outChannels, TCallback _callback)
: EsaiListener(_esai, _outChannels, [](EsaiListener*, uint32_t){})
, m_callback(std::move(_callback))
{
}

bool EsaiListenerToCallback::onDeliverAudioData(const std::vector<dsp56k::TWord>& _audioData)
{
	return m_callback(_audioData);
}
