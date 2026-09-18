#include "88lib/analog/analogModels.h"

namespace emu88Lib
{
    void configureCm32l(synthLib::OutputChain& chain, const double _samplerate)
    {
        using namespace synthLib::analogFilter;

        // PCM54HP -> IC28 demultiplexer -> six sample-and-holds, one per channel (IC25-IC27,
        // unity-gain buffers on 4700p hold capacitors), so each DAC word is held for a whole
        // frame. The three channels of a side then meet at the summing node of the first
        // low-pass, each through its own resistor: 6.8k for the two SYN pairs and 10k for the
        // reverb return. That ratio is a level, applied where the channels are summed; only
        // the node's total conductance matters here, and it sets this section's Q.
        //
        // Two inverting multiple-feedback sections, IC24b then IC24a (IC23b/IC23a on the
        // right). Both peak - together about 6 dB near 13 kHz - because they are there to
        // undo the hold's sinc droop, not to be flat on their own. The CM-32P's pair does the
        // same job with more damping; this board comes out with a few dB of lift left over.
        constexpr auto inputs = 1.0 / (1.0 / 6.8e3 + 1.0 / 6.8e3 + 1.0 / 10e3); // R74 R73 R75
        chain.filter.add(lowpass2(MultipleFeedback{inputs, 6.8e3, 6.8e3, 220e-12, 5.6e-9},
                                  _samplerate)); // R86 R67 C75 C83
        chain.filter.add(lowpass2(MultipleFeedback{10e3, 10e3, 10e3, 220e-12, 5.6e-9},
                                  _samplerate)); // R69 R61 R60 C66 C67

        // R51 2.2k into the VCA (IC20 M5207L01), then IC22b turns its current back into a
        // voltage with 4.7k || 100p. The PWM that sets the VCA's level is master volume and is
        // modelled on the board itself, not here.
        chain.filter.add(lowpass1(rcCorner(4.7e3, 100e-12), _samplerate)); // R63 C71

        // C73 10u sees R55 100k in parallel with R56 10k into the virtual ground of IC22a, an
        // inverter with 15k || 220p.
        chain.filter.add(highpass1(rcCorner(parallel(100e3, 10e3), 10e-6), _samplerate));
        chain.filter.add(lowpass1(rcCorner(15e3, 220e-12), _samplerate)); // R57 C62

        // C63 10u into R46 + R48 and R49 to ground, the divider that leaves the board.
        chain.filter.add(highpass1(rcCorner(4.7e3 + 1.5e3 + 6.8e3, 10e-6), _samplerate));
    }

    void configureCm32pReconstruction(synthLib::OutputChain& chain, const double _samplerate)
    {
        using namespace synthLib::analogFilter;

        // PCM56P -> HD14052 demultiplexer -> hold capacitor C51A buffered by IC25a, so each DAC
        // word is held for a whole frame. Then two unity-gain Sallen-Key low-passes (IC30a/b);
        // the first peaks near 14 kHz and makes up most of the hold's droop.
        chain.filter.add(lowpass2(SallenKey{10e3, 10e3, 5.6e-9, 220e-12}, _samplerate)); // R42A R43A C54A C52A
        chain.filter.add(lowpass2(SallenKey{10e3, 10e3, 1.8e-9, 1.2e-9}, _samplerate)); // R44A R45A C55A C56A
    }

    void configureCmMixerOutput(synthLib::OutputChain& chain, const double _samplerate)
    {
        using namespace synthLib::analogFilter;

        // The VCA stage (IC32 M5207L01, I/V IC34a), the mixer (IC33a) and the line amplifier
        // (IC35a) are flat in band apart from their feedback capacitors. Only the mixer's
        // 22p || 100k matters; 6.8k || 100p and 20k || 22p stay under 0.02 dB below 20 kHz.
        // The PWM-controlled VCA level is not modelled.
        //
        // On a CM-64 this is where the LA board's line output arrives, through a 100k of its
        // own into the same 100k feedback - so both boards are mixed at unity.
        chain.filter.add(lowpass1(rcCorner(100e3, 22e-12), _samplerate)); // R49A C61A

        // Output network: C62A shunts behind R57A || R60A, C58A blocks DC into R57A + R60A.
        chain.filter.add(lowpass1(rcCorner(parallel(4.7e3, 6.8e3), 1e-9), _samplerate));
        chain.filter.add(highpass1(rcCorner(4.7e3 + 6.8e3, 10e-6), _samplerate));
    }

    void configureCm32p(synthLib::OutputChain& chain, const double _samplerate)
    {
        configureCm32pReconstruction(chain, _samplerate);
        configureCmMixerOutput(chain, _samplerate);
    }

    void configureG800(synthLib::OutputChain& chain, const double _samplerate)
    {
        using namespace synthLib::analogFilter;

        // The PCM69AU holds each word for a whole frame on its current outputs; IC3b turns them
        // into a voltage with 5.6k || 220p (R49 C35). C11 47u into R29 100k and R30 12k feeds
        // IC4b, an inverter with 22k || 100p (R38 C29). R31 100R / C7 680p is far outside
        // the band, then C8 10u into R32 100k and the volume pot, taken at unity.
        chain.filter.add(lowpass1(rcCorner(5.6e3, 220e-12), _samplerate));
        chain.filter.add(lowpass1(rcCorner(22e3, 100e-12), _samplerate));

        // C3 10u into R7 12k at the virtual ground of IC1a sets the DC block; IC1a inverts
        // with 12k || 100p (R14 C27), then C1 10u into R23 100k and the jack.
        chain.filter.add(highpass1(rcCorner(12e3, 10e-6), _samplerate));
        chain.filter.add(lowpass1(rcCorner(12e3, 100e-12), _samplerate));
    }

    void configureSc55Mk1(synthLib::OutputChain& chain, const double _samplerate)
    {
        using namespace synthLib::analogFilter;

        // The uPD6376 has voltage outputs, held for a whole frame at the board's 64 kHz word
        // rate. C44 couples into R41 12k at the virtual ground of IC9a, an inverter with
        // 22k || 100p in the feedback (R31 C37).
        chain.filter.add(highpass1(rcCorner(12e3, 10e-6), _samplerate));
        chain.filter.add(lowpass1(rcCorner(22e3, 100e-12), _samplerate));

        // R51 680R / C50 100p is far outside the band. IC13a is not fitted, so R53 and R90
        // (5.6k each) lead in series into IC12a, an inverter with 18k || 82p (R37 C39).
        chain.filter.add(lowpass1(rcCorner(18e3, 82e-12), _samplerate));

        // The volume pot, taken at unity, returns into IC11a (R45 56k, R35 220k), which has no
        // capacitor. C33 10u into R27 100k, then R28 and R15 (1k each) into C25 1000p and the
        // 390p of the FL2 feed-through filter before the jack.
        chain.filter.add(lowpass1(rcCorner(2e3, 1.39e-9), _samplerate));
    }

    void configureSc55Mk2(synthLib::OutputChain& chain, const double _samplerate)
    {
        using namespace synthLib::analogFilter;

        // The uPD63200 has voltage outputs and no digital filter, held for a whole frame at the
        // board's 66.2 kHz word rate. C43 47u couples into R24 12k at the virtual ground of
        // IC10b, an inverter with 22k || 100p in the feedback (R27 C46).
        chain.filter.add(highpass1(rcCorner(12e3, 47e-6), _samplerate));
        chain.filter.add(lowpass1(rcCorner(22e3, 100e-12), _samplerate));

        // R46 100R / C56 680p is far outside the band. R47 12k into IC13a, an inverter with
        // 12k || 120p (R45 C54).
        chain.filter.add(lowpass1(rcCorner(12e3, 120e-12), _samplerate));

        // The volume pot, taken at unity, returns into IC12a (R35 12k, R36 33k), which has no
        // capacitor. C50 47u into R41 39k, then R44 and R43 (1k each) into C52 1n before the
        // ferrite bead and the jack.
        chain.filter.add(lowpass1(rcCorner(2e3, 1e-9), _samplerate));
    }

    void configureSc8820(synthLib::OutputChain& chain, const double _samplerate)
    {
        using namespace synthLib::analogFilter;

        // The PCM1716's sharp roll-off digital filter: passband to 0.454 fs within 0.002 dB,
        // stopband from 0.546 fs at 75 dB (82 dB from 0.567 fs), 30 frames of delay. MODE is
        // tied high so the firmware programs it; sharp roll-off is the default. Its internal
        // analogue filter is 3 dB down at 100 kHz, 0.16 dB at 20 kHz.
        chain.interpolator = synthLib::Interpolator(8, 0.454, 0.546, 75.0);
        chain.filter.add(lowpass1(100e3, _samplerate));

        // The DAC's voltage output: C22 10u into R23 100k and R17 10k, which leads to the
        // summing node at virtual ground, sets the first DC block.
        chain.filter.add(highpass1(rcCorner(parallel(100e3, 10e3), 10e-6), _samplerate));

        // IC31b, the same multiple-feedback low-pass as on the SC-8850 (R17 10k in, R15 18k
        // feedback, R16 10k || R18 18k to the inverting input, C19 390p, C25 2.2n), with
        // R24 100k loading the node: 16.0 kHz, Q 0.69. The gain is not modelled.
        chain.filter.add(
            lowpass2(MultipleFeedback{10e3, 18e3, parallel(10e3, 18e3), 390e-12, 2.2e-9, 100e3}, _samplerate));

        // C23 10u into the 10k volume pot VR1a, taken at unity, then R19 15k into IC33b with
        // 22k || 33p (R20 C21); R26 sums the audio input, which is not modelled. R21 470R, the
        // muting transistor, C24 10u into R25 33k and R22 100R into C26 1n before the jack.
        chain.filter.add(highpass1(rcCorner(10e3, 10e-6), _samplerate));
        chain.filter.add(lowpass1(rcCorner(22e3, 33e-12), _samplerate));
        chain.filter.add(lowpass1(rcCorner(470.0 + 100.0, 1e-9), _samplerate));
    }

    void configureSc8850(synthLib::OutputChain& chain, const double _samplerate)
    {
        using namespace synthLib::analogFilter;

        // The AK4324's digital filter: passband to 0.4535 fs within 0.005 dB, stopband from
        // 0.546 fs at 75 dB, group delay 27 frames. Its second-order switched-capacitor
        // post-filter is not specified beyond 0.2 dB to 20 kHz and is not modelled.
        chain.interpolator = synthLib::Interpolator(8, 0.4535, 0.546, 75.0);

        // IC46a takes the differential outputs with 22k all round and 100p on both sides
        // (R87 C148, R90 C149). C18 33u into R19 100k and R12 10k sets the DC block.
        chain.filter.add(lowpass1(rcCorner(22e3, 100e-12), _samplerate));
        chain.filter.add(highpass1(rcCorner(parallel(100e3, 10e3), 33e-6), _samplerate));

        // IC3a, marked "fc=16kHz +5.1dB" on the schematic: a multiple-feedback low-pass with
        // R12 10k in, R10 18k feedback, R11 10k || R13 18k to the inverting input, C16 390p
        // across the op-amp and C21 2.2n to ground. 16.0 kHz, Q 0.71; the gain is not modelled.
        chain.filter.add(lowpass2(MultipleFeedback{10e3, 18e3, parallel(10e3, 18e3), 390e-12, 2.2e-9}, _samplerate));

        // The volume pot at unity, IC2b with 10k || 33p (R15 C17), then R16 470R, the muting
        // transistor, C20 33u into R20 33k and R17 100R into C22 1n before the jack.
        chain.filter.add(lowpass1(rcCorner(10e3, 33e-12), _samplerate));
        chain.filter.add(lowpass1(rcCorner(470.0 + 100.0, 1e-9), _samplerate));
    }

    void configureSc88(synthLib::OutputChain& chain, const double _samplerate)
    {
        using namespace synthLib::analogFilter;

        // The PCM69AU holds each word for a whole frame on its current outputs, which IC110a/b
        // turn into a voltage with 4.7k || 100p (R146 C152). There is no reconstruction filter:
        // every stage that follows is flat well past the band, so the hold's droop and images
        // reach the output almost as they are.
        chain.filter.add(lowpass1(rcCorner(4.7e3, 100e-12), _samplerate));

        // C147 couples into IC109b, an inverter with 22k || 100p in the feedback (R140 C144).
        // Its 12k input (R142) with R144 100k to ground sets the DC block.
        chain.filter.add(highpass1(rcCorner(parallel(100e3, 12e3), 47e-6), _samplerate));
        chain.filter.add(lowpass1(rcCorner(22e3, 100e-12), _samplerate));

        // R138 100R / C141 680p and the C139 47u / R136 100k coupling into the volume pot are
        // outside the band; the pot is taken at unity. Its return feeds IC108a, an inverter
        // with 12k || 120p in the feedback (R127 C131).
        chain.filter.add(lowpass1(rcCorner(12e3, 120e-12), _samplerate));

        // IC102a drives the line output through the muting FET, C122 47u into R107 100k, and
        // R101 1k / C113 390p before the ferrite bead and the jack.
        chain.filter.add(lowpass1(rcCorner(1e3, 390e-12), _samplerate));
    }

    void configureSc88Pro(synthLib::OutputChain& chain, const double _samplerate)
    {
        using namespace synthLib::analogFilter;

        // The PCM69AU holds each word for a whole frame on its current outputs. IC112b turns
        // them into a voltage with 8.2k || 330p (R174 C170); its non-inverting input only
        // carries the DAC's common-mode bias.
        chain.filter.add(lowpass1(rcCorner(8.2e3, 330e-12), _samplerate));

        // C192 couples into R133 15k to ground and the filter, whose DC input resistance is
        // R110 + R180 because the summing node sits at virtual ground at low frequencies.
        chain.filter.add(highpass1(rcCorner(parallel(15e3, 13.6e3), 33e-6), _samplerate));

        // The reconstruction filter: R110 / C150 feed the multiple-feedback stage IC104b
        // (R180, R808 + R807 as feedback, R177, C168, C169) directly, a third-order low-pass
        // with a real pole at 25.4 kHz and a pair at 14.8 kHz, Q 0.80. Unity DC gain.
        for (const auto& section : lowpass3({6.8e3, 2.7e-9, {6.8e3, 13.6e3, 6.8e3, 390e-12, 2.2e-9}}, _samplerate))
            chain.filter.add(section);

        // C133 couples into the volume pot, taken at unity. IC102a is an inverter with
        // 6.8k || 330p (R108 C172); R128 sums the audio input, which is not modelled.
        chain.filter.add(lowpass1(rcCorner(6.8e3, 330e-12), _samplerate));

        // The muting FET, C194 47u into R115 100k, then R116 220R with C135 2.2n to ground
        // and R151 220R before the ferrite bead and the jack.
        chain.filter.add(lowpass1(rcCorner(220.0, 2.2e-9), _samplerate));
    }

    void configureSc88Vl(synthLib::OutputChain& chain, const double _samplerate)
    {
        using namespace synthLib::analogFilter;

        // The uPD63200 is a resistor-string DAC with its own output op-amp and no digital
        // filter, so it holds each word for a whole frame. R27 100R / C42 680p is far outside
        // the band. C46 couples into the low-pass, whose summing node sits at virtual ground at
        // low frequencies, so the DC block is set by R29 100k || R32 12k.
        chain.filter.add(highpass1(rcCorner(parallel(100e3, 12e3), 47e-6), _samplerate));

        // IC15a, a multiple-feedback low-pass (R32 R36 R34 C52 C48): 25.5 kHz, Q 0.62. Its
        // DC gain of 22k / 12k is not modelled.
        chain.filter.add(lowpass2(MultipleFeedback{12e3, 22e3, 12e3, 180e-12, 820e-12}, _samplerate));

        // C58 couples into the volume pot, taken at unity. IC18b (R57 R48, both 33k) is a flat
        // inverter. R50 1k, the muting transistor and C62 47u into R46 100k follow, then
        // R47 470R with C63 1n to ground: the pole sees R50 + R47.
        chain.filter.add(lowpass1(rcCorner(1e3 + 470.0, 1e-9), _samplerate));
    }

    void configureScc1(synthLib::OutputChain& chain, const double _samplerate)
    {
        using namespace synthLib::analogFilter;

        // The uPD6376 has voltage outputs, held for a whole frame at the 64 kHz word rate. C23
        // couples into R37 4.7k and R36 2.2k, which lead to the virtual ground of IC4a. C25 10n
        // at their junction is loaded by both, a 10.6 kHz pole: the card's only real low-pass.
        chain.filter.add(highpass1(rcCorner(4.7e3 + 2.2e3, 10e-6), _samplerate));
        chain.filter.add(lowpass1(rcCorner(parallel(4.7e3, 2.2e3), 10e-9), _samplerate));

        // IC4a inverts with 15k || 330p (R35 C24); R33 12k into IC4b with 47k || 68p (R34 C19A).
        chain.filter.add(lowpass1(rcCorner(15e3, 330e-12), _samplerate));
        chain.filter.add(lowpass1(rcCorner(47e3, 68e-12), _samplerate));

        // C18 10u into R13 47k, the muting transistor, then R11 1k and R14 680R into C4 1n
        // before the ferrite bead and the jack.
        chain.filter.add(lowpass1(rcCorner(1e3 + 680.0, 1e-9), _samplerate));
    }
} // namespace emu88Lib
