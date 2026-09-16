#pragma once
#include "synthLib/analog/analogFilter.h"

namespace emu88Lib
{
    void configureCm32p(synthLib::OutputChain& chain, const double _samplerate);
    void configureG800(synthLib::OutputChain& chain, const double _samplerate);
    void configureSc55Mk1(synthLib::OutputChain& chain, const double _samplerate);
    void configureSc55Mk2(synthLib::OutputChain& chain, const double _samplerate);
    void configureSc8820(synthLib::OutputChain& chain, const double _samplerate);
    void configureSc8850(synthLib::OutputChain& chain, const double _samplerate);
    void configureSc88(synthLib::OutputChain& chain, const double _samplerate);
    void configureSc88Pro(synthLib::OutputChain& chain, const double _samplerate);
    void configureSc88Vl(synthLib::OutputChain& chain, const double _samplerate);
    void configureScc1(synthLib::OutputChain& chain, const double _samplerate);
} // namespace emu88Lib
