#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace synthLib
{
    // b0, b1, b2, a1, a2 with a0 = 1.
    using BiquadCoefficients = std::array<double, 5>;

    // Discretised analogue sections that output circuits are assembled from.
    // Component values are in ohms and farads.
    namespace analogFilter
    {
        double rcCorner(double _r, double _c);
        double parallel(double _a, double _b);

        // Unity-gain Sallen-Key low-pass: c1 is the capacitor to the output, c2 the one to ground.
        struct SallenKey
        {
            double r1, r2, c1, c2;

            double frequency() const;
            double q() const;
        };

        // Inverting multiple-feedback low-pass: r1 from the input to the summing node, rf from
        // the node to the output, rg from the node to the inverting input, cf across the op-amp
        // and cg from the node to ground. An optional rn from the node to ground only lowers
        // the Q. DC gain is rf / r1, not modelled.
        struct MultipleFeedback
        {
            double r1, rf, rg, cf, cg;
            double rn = 0.0;

            double nodeConductance() const;

            double frequency() const;
            double q() const;
        };

        // The analogue poles, with the analogue magnitude matched at DC, at the corner and at
        // Nyquist (M. Vicanek, "Matched Second Order Digital Filters"). The bilinear transform
        // would warp the response towards Nyquist, where the sample-and-hold images are.
        BiquadCoefficients lowpass2(double _frequency, double _q, double _samplerate);
        BiquadCoefficients lowpass2(const SallenKey& _stage, double _samplerate);
        BiquadCoefficients lowpass2(const MultipleFeedback& _stage, double _samplerate);

        // A passive rin / cin section feeding a multiple-feedback low-pass with no buffer in
        // between: the three capacitors interact and form one third-order low-pass.
        struct RcMultipleFeedback
        {
            double rin, cin;
            MultipleFeedback stage;
        };

        // The third-order response as a first-order and a second-order section.
        std::array<BiquadCoefficients, 2> lowpass3(const RcMultipleFeedback& _network, double _samplerate);

        // The analogue pole, with the analogue magnitude matched at DC and at the top of the
        // audio band (20 kHz), including corners above Nyquist.
        BiquadCoefficients lowpass1(double _frequency, double _samplerate);

        // Bilinear transform, for corners far below Nyquist such as DC blocking.
        BiquadCoefficients highpass1(double _frequency, double _samplerate);
    } // namespace analogFilter

    // The digital interpolation filter of an oversampling DAC: a linear-phase low-pass designed
    // with a Kaiser window and applied polyphase, so it reads each frame once and costs
    // taps / factor multiplies per output sample. Passband and stopband edges are fractions
    // of the frame rate; the stopband reaches the given attenuation. Frames are expected to
    // arrive held for factor calls, the first call after construction or reset being a new one.
    class Interpolator
    {
    public:
        Interpolator() = default;
        Interpolator(uint32_t _factor, double _passband, double _stopband, double _attenuationDb);

        void reset();
        void process(float& _left, float& _right);
        size_t taps() const { return m_taps.size(); }

    private:
        uint32_t m_factor = 1;
        uint32_t m_phase = 0;
        size_t m_frames = 0;
        size_t m_position = 0;
        std::vector<double> m_taps;
        std::array<std::vector<double>, 2> m_history;
    };

    // A stereo cascade of biquad sections in transposed direct form II.
    class FilterCascade
    {
    public:
        void add(const BiquadCoefficients& _coefficients);
        void reset();
        void process(float& _left, float& _right);

    private:
        static constexpr size_t MaxSections = 8;

        struct Section
        {
            double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        };

        struct State
        {
            double z1 = 0.0, z2 = 0.0;
        };

        std::array<Section, MaxSections> m_sections{};
        size_t m_sectionCount = 0;
        std::array<std::array<State, MaxSections>, 2> m_state{};
    };
    // A stereo reconstruction chain, optionally preceded by DAC interpolation.
    struct OutputChain
    {
        Interpolator interpolator;
        FilterCascade filter;
        void reset()
        {
            interpolator.reset();
            filter.reset();
        }
        void process(float& left, float& right)
        {
            interpolator.process(left, right);
            filter.process(left, right);
        }
    };

} // namespace synthLib
