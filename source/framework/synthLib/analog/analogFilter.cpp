#include "analogFilter.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace synthLib
{
    namespace
    {
        constexpr double g_pi = 3.14159265358979323846;

        // Where lowpass1 matches the analogue magnitude: the top of the audio band.
        constexpr double g_matchFrequency = 20000.0;
    } // namespace

    namespace analogFilter
    {
        double rcCorner(const double _r, const double _c) { return 1.0 / (2.0 * g_pi * _r * _c); }

        double parallel(const double _a, const double _b) { return _a * _b / (_a + _b); }

        double SallenKey::frequency() const { return 1.0 / (2.0 * g_pi * std::sqrt(r1 * r2 * c1 * c2)); }

        double SallenKey::q() const { return std::sqrt(r1 * r2 * c1 * c2) / (c2 * (r1 + r2)); }

        double MultipleFeedback::frequency() const { return 1.0 / (2.0 * g_pi * std::sqrt(rg * rf * cf * cg)); }

        double MultipleFeedback::nodeConductance() const
        {
            return 1.0 / r1 + 1.0 / rf + 1.0 / rg + (rn > 0.0 ? 1.0 / rn : 0.0);
        }

        double MultipleFeedback::q() const { return std::sqrt(rg * rf * cf * cg) / (rg * rf * cf * nodeConductance()); }

        BiquadCoefficients lowpass2(const SallenKey& _stage, const double _samplerate)
        {
            return lowpass2(_stage.frequency(), _stage.q(), _samplerate);
        }

        BiquadCoefficients lowpass2(const MultipleFeedback& _stage, const double _samplerate)
        {
            return lowpass2(_stage.frequency(), _stage.q(), _samplerate);
        }

        BiquadCoefficients lowpass2(const double _frequency, const double _q, const double _samplerate)
        {
            const double w0 = 2.0 * g_pi * _frequency / _samplerate;
            assert(w0 < g_pi && "corner must lie below Nyquist");
            const double q = _q;
            const double zeta = 1.0 / (2.0 * q);
            const double decay = std::exp(-zeta * w0);
            const double a1 = zeta <= 1.0 ? -2.0 * decay * std::cos(std::sqrt(1.0 - zeta * zeta) * w0)
                                          : -2.0 * decay * std::cosh(std::sqrt(zeta * zeta - 1.0) * w0);
            const double a2 = decay * decay;

            // Vicanek's closed form: the squared numerator magnitude is B0 at DC and B1 at
            // Nyquist, both chosen so the response also passes through the analogue gain at w0.
            const double A0 = (1.0 + a1 + a2) * (1.0 + a1 + a2);
            const double A1 = (1.0 - a1 + a2) * (1.0 - a1 + a2);
            const double A2 = -4.0 * a2;
            const double phi1 = std::sin(0.5 * w0) * std::sin(0.5 * w0);
            const double phi0 = 1.0 - phi1;
            const double phi2 = 4.0 * phi0 * phi1;
            const double R1 = (A0 * phi0 + A1 * phi1 + A2 * phi2) * q * q;
            const double B0 = A0;
            const double B1 = std::max(0.0, (R1 - B0 * phi0) / phi1);
            const double b0 = 0.5 * (std::sqrt(B0) + std::sqrt(B1));
            return {b0, std::sqrt(B0) - b0, 0.0, a1, a2};
        }

        std::array<BiquadCoefficients, 2> lowpass3(const RcMultipleFeedback& _network, const double _samplerate)
        {
            // Nodal analysis with the op-amp's inverting input at virtual ground. v1 is the node
            // after rin, v2 the summing node: v2 = -s rg cf vout, g2 v1 = v2 (g2 + gf + gg + s cg)
            // - gf vout and g1 vin = v1 (g1 + g2 + s cin) - g2 v2. The denominator is a cubic in s
            // with unity DC gain once normalised.
            const double g1 = 1.0 / _network.rin;
            const double g2 = 1.0 / _network.stage.r1;
            const double gf = 1.0 / _network.stage.rf;
            const double gg = 1.0 / _network.stage.rg;
            const double cin = _network.cin;
            const double cf = _network.stage.cf;
            const double cg = _network.stage.cg;
            const double k1 = cf * _network.stage.nodeConductance() / gg;
            const double k2 = cf * cg / gg;
            const double d0 = (g1 + g2) * gf;
            const double d1 = ((g1 + g2) * k1 + cin * gf - g2 * g2 * cf / gg) / d0;
            const double d2 = ((g1 + g2) * k2 + cin * k1) / d0;
            const double d3 = cin * k2 / d0;

            // Monic cubic s^3 + a s^2 + b s + c. It is positive at zero and negative at minus the
            // Cauchy bound, so bisection finds a real root; deflating leaves the other two.
            const double a = d2 / d3;
            const double b = d1 / d3;
            const double c = 1.0 / d3;
            double lo = -(1.0 + std::max(std::fabs(a), std::max(std::fabs(b), std::fabs(c))));
            double hi = 0.0;
            for (int i = 0; i < 200; ++i)
            {
                const double mid = 0.5 * (lo + hi);
                const double value = ((mid + a) * mid + b) * mid + c;
                (value > 0.0 ? hi : lo) = mid;
            }
            const double realPole = 0.5 * (lo + hi);
            const double p = a + realPole; // s^2 + p s + q
            const double q = b + realPole * p;
            assert(realPole < 0.0 && p > 0.0 && q > 0.0);

            const double w0 = std::sqrt(q);
            return {lowpass1(-realPole / (2.0 * g_pi), _samplerate), lowpass2(w0 / (2.0 * g_pi), w0 / p, _samplerate)};
        }

        BiquadCoefficients lowpass1(const double _frequency, const double _samplerate)
        {
            assert(_samplerate > 2.0 * g_matchFrequency);
            const double pole = std::exp(-2.0 * g_pi * _frequency / _samplerate);
            const double sum = 1.0 - pole; // b0 + b1: unity at DC

            // b1 such that |b0 + b1 z^-1| equals the analogue magnitude times |1 - pole z^-1|
            // at the match frequency. Of the two solutions take the one nearer to the plain
            // analogue pole, which passes a corner far above the band through unchanged.
            const double w = 2.0 * g_pi * g_matchFrequency / _samplerate;
            const double ratio = g_matchFrequency / _frequency;
            const double target = (1.0 - 2.0 * pole * std::cos(w) + pole * pole) / (1.0 + ratio * ratio);
            const double a = 2.0 * (1.0 - std::cos(w));
            const double b = -sum * a;
            const double c = sum * sum - target;
            const double discriminant = std::max(0.0, b * b - 4.0 * a * c);
            const double b1 = (-b - std::sqrt(discriminant)) / (2.0 * a);
            return {sum - b1, b1, 0.0, -pole, 0.0};
        }

        BiquadCoefficients highpass1(const double _frequency, const double _samplerate)
        {
            const double k = std::tan(g_pi * _frequency / _samplerate);
            const double norm = 1.0 / (1.0 + k);
            return {norm, -norm, 0.0, (k - 1.0) * norm, 0.0};
        }
    } // namespace analogFilter

    namespace
    {
        double besselI0(const double _x)
        {
            double sum = 1.0;
            double term = 1.0;
            const double q = 0.25 * _x * _x;
            for (int k = 1; k < 500; ++k)
            {
                term *= q / (static_cast<double>(k) * static_cast<double>(k));
                sum += term;
                if (term < sum * 1e-15)
                    break;
            }
            return sum;
        }
    } // namespace

    Interpolator::Interpolator(const uint32_t _factor, const double _passband, const double _stopband,
                               const double _attenuationDb) : m_factor(_factor)
    {
        // Kaiser's design formulas, in radians of the output rate.
        const double beta = _attenuationDb > 50.0 ? 0.1102 * (_attenuationDb - 8.7)
            : _attenuationDb >= 21.0 ? 0.5842 * std::pow(_attenuationDb - 21.0, 0.4) + 0.07886 * (_attenuationDb - 21.0)
                                     : 0.0;
        const double transition = 2.0 * g_pi * (_stopband - _passband) / _factor;
        const double cutoff = g_pi * (_passband + _stopband) / _factor;
        const auto length = static_cast<size_t>(std::ceil((_attenuationDb - 7.95) / (2.285 * transition))) + 1;
        m_frames = (length + _factor - 1) / _factor;
        m_taps.resize(m_frames * _factor);

        const double middle = 0.5 * static_cast<double>(m_taps.size() - 1);
        const double i0Beta = besselI0(beta);
        double sum = 0.0;
        for (size_t n = 0; n < m_taps.size(); ++n)
        {
            const double t = static_cast<double>(n) - middle;
            const double sinc = t == 0.0 ? cutoff / g_pi : std::sin(cutoff * t) / (g_pi * t);
            const double r = t / middle;
            const double window = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0Beta;
            m_taps[n] = sinc * window;
            sum += m_taps[n];
        }
        // Unity gain at DC for the zero-stuffed input: every polyphase branch sums to about one.
        for (auto& tap : m_taps)
            tap *= static_cast<double>(_factor) / sum;

        for (auto& history : m_history)
            history.assign(m_frames, 0.0);
    }

    void Interpolator::reset()
    {
        m_phase = 0;
        m_position = 0;
        for (auto& history : m_history)
            std::fill(history.begin(), history.end(), 0.0);
    }

    void Interpolator::process(float& _left, float& _right)
    {
        if (m_frames == 0)
            return;

        float* const samples[2] = {&_left, &_right};

        if (m_phase == 0)
        {
            m_position = (m_position + 1) % m_frames;
            m_history[0][m_position] = _left;
            m_history[1][m_position] = _right;
        }

        for (size_t channel = 0; channel < 2; ++channel)
        {
            double acc = 0.0;
            size_t index = m_position;
            for (size_t k = 0; k < m_frames; ++k)
            {
                acc += m_taps[k * m_factor + m_phase] * m_history[channel][index];
                index = index == 0 ? m_frames - 1 : index - 1;
            }
            *samples[channel] = static_cast<float>(acc);
        }

        if (++m_phase == m_factor)
            m_phase = 0;
    }

    void FilterCascade::add(const BiquadCoefficients& _coefficients)
    {
        assert(m_sectionCount < m_sections.size() && "raise MaxSections");
        if (m_sectionCount == m_sections.size())
            return;

        auto& section = m_sections[m_sectionCount++];
        section.b0 = _coefficients[0];
        section.b1 = _coefficients[1];
        section.b2 = _coefficients[2];
        section.a1 = _coefficients[3];
        section.a2 = _coefficients[4];
    }

    void FilterCascade::reset() { m_state = {}; }

    void FilterCascade::process(float& _left, float& _right)
    {
        float* const samples[2] = {&_left, &_right};

        for (size_t channel = 0; channel < 2; ++channel)
        {
            double x = *samples[channel];

            for (size_t i = 0; i < m_sectionCount; ++i)
            {
                const auto& section = m_sections[i];
                auto& state = m_state[channel][i];

                const double y = section.b0 * x + state.z1;
                state.z1 = section.b1 * x - section.a1 * y + state.z2;
                state.z2 = section.b2 * x - section.a2 * y;
                x = y;
            }

            *samples[channel] = static_cast<float>(x);
        }
    }
} // namespace synthLib
