// license:BSD-3-Clause
// copyright-holders:Olivier Galibert

// How an accurate resampler works ?

// Resampling uses a number of well-known theorems we are not trying
// to prove here.

// Samping theorem.  A digital signal sampled at frequency fs is
// equivalent to an analog signal where all frequencies are between 0
// and fs/2.  Equivalent here means that the samples are unique given
// the analog signal, the analog sugnal is unique given the samples,
// and going analog -> digital -> analog is perfect.

// That gives us point one: resampling from fs to ft is, semantically,
// reconstructing the analog signal from the fs sampling, removing all
// frequencies over ft/2, then sampling at ft.


// Up-sampling theorem.  Take a digital signal at frequency fs, and k
// an integer > 1.  Create a new digital signal at frequency fs*k by
// alternatively taking one sample from the original signal and adding
// k-1 zeroes.  If one recreates the corresponding analog signal and
// removes all frequencies over fs/2, then it will be identical to the
// original analog signal, up to a constant multiplier on the
// amplitude. For the curious the frequencies over fs/2 get copies of
// the original spectrum with inversions, e.g. the frequency fs/2-a is
// copied at fs/2+a, then it's not inverted at fs..fs*1.5, inverted
// again between fs*1.5 and fs*2, etc.

// A corollary is that if one starts for an analog signal with no
// frequencies over fs/2, samples it at fs, then up-samples to fs*k by
// adding zeroes, remove (filter) from the upsampled signal all
// frequencies over fs/2 then reconstruct the analog signal you get a
// result identical to the original signal.  It's a perfect
// upsampling, assuming the filtering is perfect.


// Down-sampling theorem.  Take a digital signal at frequency ft*k,
// with k and integer > 1.  Create a new digital signal at frequency
// ft by alternatively taking one sample from the original signal and
// dropping k-1 samples.  If the original signal had no frequency over
// ft/2, then the reconstructed analog signal is identical to the
// original one, up to a constant multiplier on the amplitude.  So it
// is a perfect downsampling assuming the original signal has nothing
// over ft/2.  For the curious if there are frequencies over ft/2,
// they end up added to the lower frequencies with inversions.  The
// frequency ft/2+a is added to ft/2-a, etc (signal to upsampling,
// only the other way around).

// The corollary there is that if one starts with a ft*k digital
// signal, filters out everything over ft/2, then keeps only one
// sample every k, then reconstruct the analog signal, you get the
// original analog signal with frequencies over ft/2 removed, which is
// reasonable given they are not representable at sampling frequency
// ft anyway.  As such it is called perfect because it's the best
// possible result in any case.

// Incidentally, the parasite audible frequencies added with the
// wrapping when the original is insufficiently filtered before
// dropping the samples are called aliasing, as in the high barely
// audible frequencies that was there but not noticed gets aliased to
// a very audible and annoying lower frequency.


// As a result, the recipe to go from frequency fs to ft for a digital
// signal is:

// - find a frequency fm = ks*fs = kt*ft with ks and kt integers.
//   When fs and ft are integers (our case), the easy solution is
//   fm = fs * ft / gcd(fs, ft)

// - up-sample the original signal x(t) into xm(t) with:
//      xm(ks*t) = x(t)
//      xm(other) = 0

// - filter the resulting fm Hz signal to remove all frequencies above
//   fs/2.  This is also called "lowpass at fs/2"

// - lowpass at ft/2

// - down-sample the fm signal into the resulting y(t) signal by:
//      y(t) = xm(kt*t)

// And, assuming the filtering is perfect (it isn't, of course), the
// result is a perfect resampling.

// Now to optimize all that.  The first point is that an ideal lowpass
// at fs/2 followed by an ideal lowpass at ft/2 is strictly equivalent
// to an ideal lowpass at min(fs/2, ft/2).  So only one filter is
// needed.

// The second point depends on the type of filter used.  In our case
// the filter type known as FIR has a big advantage.  A FIR filter
// computes the output signal as a finite ponderated sum on the values
// of the input signal only (also called a convolution).  E.g.
//    y(t) = sum(k=0, n-1) a[k] * x[t-k]
// where a[0..n-1] are constants called the coefficients of the filter.

// Why this type of filter is pertinent shows up when building the
// complete computation:

// y(t) = filter(xm)[kt*t]
//      = sum(k=0, n-1) a[k] * xm[kt*t - k]
//      = sum(k=0, n-1) a[k] * | x[(kt*t-k)/ks] when kt*t-k is divisible by ks
//                             | 0              otherwise
//      = sum(k=(kt*t) mod ks, n-1, step=ks) a[k] * x[(kt*t-k)/ks]

//        (noting p = (kt*t) mode ks, and a // b integer divide of a by b)
//      = sum(k=0, (n-1 - p))//ks) a[k*ks + p] x[(kt*t) // ks) - k]

// Splitting the filter coefficients in ks phases ap[0..ks-1] where
// ap[p][k] = a[p + ks*k], and noting t0 = (k*kt) // ks:

// y(t) = sum(k=0, len(ap[p])-1) ap[p][k] * x[t0-k]

// So we can take a big FIR filter and split it into ks interpolation
// filters and just apply the correct one at each sample.  We can make
// things even easier by ensuring that the size of every interpolation
// filter is the same.

// The art of creating the big FIR filter so that it doesn't change
// the signal too much is complicated enough that entire books have
// been written on the topic.  We use here a simple solution which is
// to use a so-called zero-phase filter, which is a symmetrical filter
// which looks into the future to filter out the frequencies without
// changing the phases, and shift it in the past by half its length,
// making it causal (e.g. not looking into the future anymore).  It is
// then called linear-phase, and has a latency of exactly half its
// length.  The filter itself is made very traditionally, by
// multiplying a sinc by a Hann window.

// The default filter size is selected by maximizing the latency to 5ms
// and capping the length at 400, which experimentally seems to ensure
// a sharp rejection of more than 100dB in every case.

// Finally, remember that up and downsampling steps multiply the
// amplitude by a constant (upsampling divides by k, downsampling
// multiplies by k in fact).  To compensate for that and numerical
// errors the easiest way to to normalize each phase-filter
// independently to ensure the sum of their coefficients is 1.  It is
// easy to see why it works: a constant input signal must be
// transformed into a constant output signal at the exact same level.
// Having the sum of coefficients being 1 ensures that.

#include "mameResamplers.h"

#include <algorithm>
#include <cmath>

namespace synthLib
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;

        static float sample_at(const std::deque<float>& src, const int64_t srcBase, const int64_t index)
        {
            if (index < srcBase)
                return 0.0f;

            const int64_t off = index - srcBase;
            if (off < 0 || static_cast<size_t>(off) >= src.size())
                return 0.0f;

            return src[static_cast<size_t>(off)];
        }
    }

    std::unique_ptr<MameResampler> MameResampler::create(const MameResamplerMode mode, const uint32_t fs, const uint32_t ft)
    {
        if (mode == MameResamplerMode::Lofi)
            return std::make_unique<MameResamplerLofi>(fs, ft);
        return std::make_unique<MameResamplerHq>(fs, ft);
    }

    // High-quality resampler:
    // - Polyphase FIR derived from a windowed sinc
    // - Per-phase normalization to preserve DC gain
    MameResamplerHq::MameResamplerHq(const uint32_t fs, const uint32_t ft, const float latency, const uint32_t maxOrderPerLane, const uint32_t maxLanes)
        : m_ft(ft)
        , m_fs(fs)
    {
        const uint32_t gcd = computeGcd(fs, ft);
        m_ftm = fs / gcd;
        m_fsm = ft / gcd;

        m_orderPerLane = std::clamp(static_cast<uint32_t>(fs * latency * 2), 2u, maxOrderPerLane);

        m_phaseShift = 0;
        while (((m_fsm - 1) >> m_phaseShift) >= maxLanes)
            ++m_phaseShift;

        m_phases = ((m_fsm - 1) >> m_phaseShift) + 1;

        uint32_t filterLength = m_orderPerLane * m_phases;
        if ((filterLength & 1) == 0)
            --filterLength;
        const uint32_t hlen = std::max(1u, filterLength / 2);

        m_coefficients.resize(m_phases);
        for (uint32_t i = 0; i != m_phases; ++i)
            m_coefficients[i].resize(m_orderPerLane, 0.0f);

        const double cutoff = std::min(fs / 2.0, ft / 2.0);
        auto set_filter = [this](const uint32_t i, const float v)
        {
            m_coefficients[i % m_phases][i / m_phases] = v;
        };

        const double wc = 2.0 * kPi * cutoff / (double(fs) * double(m_fsm) / double(1u << m_phaseShift));
        const double a = wc / kPi;

        for (uint32_t i = 1; i != hlen; ++i)
        {
            double win = std::cos(i * kPi / hlen / 2.0);
            win *= win;
            const double s = a * std::sin(i * wc) / (i * wc) * win;

            set_filter(hlen - 1 + i, static_cast<float>(s));
            set_filter(hlen - 1 - i, static_cast<float>(s));
        }
        set_filter(hlen - 1, static_cast<float>(a));

        for (uint32_t i = 0; i != m_phases; ++i)
        {
            float s = 0.0f;
            for (uint32_t j = 0; j != m_orderPerLane; ++j)
                s += m_coefficients[i][j];
            const float inv = (s != 0.0f) ? (1.0f / s) : 1.0f;
            for (uint32_t j = 0; j != m_orderPerLane; ++j)
                m_coefficients[i][j] *= inv;
        }

        m_delta = m_ftm % m_fsm;
        m_skip = m_ftm / m_fsm;
    }

    uint32_t MameResamplerHq::computeGcd(uint32_t fs, uint32_t ft)
    {
        uint32_t v1 = fs > ft ? fs : ft;
        uint32_t v2 = fs > ft ? ft : fs;
        while (v2)
        {
            const uint32_t v3 = v1 % v2;
            v1 = v2;
            v2 = v3;
        }
        return v1;
    }

    uint32_t MameResamplerHq::historySize() const
    {
        return m_orderPerLane + m_skip + 1;
    }

    int64_t MameResamplerHq::minSourceIndexForOutput(const uint64_t destSample) const
    {
        const uint64_t seconds = destSample / m_ft;
        const uint64_t dsamp = destSample % m_ft;
        const uint64_t ssamp = (dsamp * m_fs) / m_ft;
        const int64_t ssample = static_cast<int64_t>(ssamp + uint64_t(m_fs) * seconds);
        return ssample - static_cast<int64_t>(m_orderPerLane) + 1;
    }

    int64_t MameResamplerHq::maxSourceIndexNeeded(const uint64_t destSample, const uint32_t samples) const
    {
        uint64_t seconds = destSample / m_ft;
        const uint32_t dsamp = static_cast<uint32_t>(destSample % m_ft);
        const uint32_t ssamp = static_cast<uint32_t>((uint64_t(dsamp) * m_fs) / m_ft);
        int64_t s = static_cast<int64_t>(ssamp + uint64_t(m_fs) * seconds);
        uint32_t phase = (dsamp * m_ftm) % m_fsm;

        int64_t maxS = s;
        for (uint32_t sample = 0; sample != samples; ++sample)
        {
            maxS = std::max(maxS, s);
            phase += m_delta;
            s += m_skip;
            while (phase >= m_fsm)
            {
                phase -= m_fsm;
                ++s;
            }
            if ((destSample + sample + 1) % m_ft == 0)
                ++seconds;
        }
        return maxS;
    }

    void MameResamplerHq::apply(const std::deque<float>& src, const int64_t srcBase, float* dest, const uint64_t destSample, const uint32_t samples, const float gain) const
    {
        if (samples == 0)
            return;

        const uint64_t seconds = destSample / m_ft;
        const uint32_t dsamp = static_cast<uint32_t>(destSample % m_ft);
        const uint32_t ssamp = static_cast<uint32_t>((uint64_t(dsamp) * m_fs) / m_ft);
        int64_t s = static_cast<int64_t>(ssamp + uint64_t(m_fs) * seconds);
        uint32_t phase = (dsamp * m_ftm) % m_fsm;

        // Copy needed source range to contiguous buffer for cache-friendly inner loop
        const int64_t rangeStart = s - static_cast<int64_t>(m_orderPerLane) + 1;
        const int64_t rangeEnd = maxSourceIndexNeeded(destSample, samples);
        const size_t rangeSize = static_cast<size_t>(rangeEnd - rangeStart + 1);

        m_scratchBuffer.resize(rangeSize);
        for (size_t i = 0; i < rangeSize; ++i)
            m_scratchBuffer[i] = sample_at(src, srcBase, rangeStart + static_cast<int64_t>(i));

        for (uint32_t sample = 0; sample != samples; ++sample)
        {
            float acc = 0.0f;
            const float* filter = m_coefficients[phase >> m_phaseShift].data();
            const size_t srcOff = static_cast<size_t>(s - rangeStart);
            for (uint32_t k = 0; k != m_orderPerLane; ++k)
                acc += filter[k] * m_scratchBuffer[srcOff - k];
            dest[sample] += acc * gain;

            phase += m_delta;
            s += m_skip;
            while (phase >= m_fsm)
            {
                phase -= m_fsm;
                ++s;
            }
        }
    }

    // Now for the lo-fi version
    //
    // We mostly forget about filtering, and just try to do a decent
    // interpolation.  There's a nice 4-point formula used in yamaha
    // devices from around 2000:
    //   f0(t) = (t - t**3)/6
    //   f1(t) = t + (t**2 - t**3)/2
    //
    // The polynoms are used with the decimal part 'p' (as in phase) of
    // the sample position.  The computation from the four samples s0..s3
    // is:
    //   s = - s0 * f0(1-p) + s1 * f1(1-p) + s2 * f1(p) - s3 * f0(p)
    //
    // The target sample must be between s1 and s2.
    //
    // When upsampling, that's enough.  When downsampling, it feels like a
    // good idea to filter a little with a moving average, dividing the
    // source frequency by an integer just big enough to make the final
    // source frequency lower.

    // Sample interpolation functions f0 and f1
    const std::array<std::array<float, 0x1001>, 2> MameResamplerLofi::s_interpolationTable = []()
    {
        std::array<std::array<float, 0x1001>, 2> result{};

        for (uint32_t i = 1; i != 4096; ++i)
        {
            const float p = i / 4096.0f;
            result[0][i] = (p - p * p * p) / 6.0f;
        }
        for (uint32_t i = 1; i != 2049; ++i)
        {
            const float p = i / 4096.0f;
            result[1][i] = p + (p * p - p * p * p) / 2.0f;
        }
        for (uint32_t i = 2049; i != 4096; ++i)
            result[1][i] = 1.0f + result[0][i] + result[0][4096 - i] - result[1][4096 - i];

        result[0][0] = 0.0f;
        result[0][0x1000] = 0.0f;
        result[1][0] = 0.0f;
        result[1][0x1000] = 1.0f;
        return result;
    }();

    MameResamplerLofi::MameResamplerLofi(const uint32_t fs, const uint32_t ft)
        : m_sourceDivide(fs <= ft ? 1u : 1u + fs / ft)
        , m_invSourceDivide(1.0f / static_cast<float>(m_sourceDivide))
        , m_fs(fs)
        , m_ft(ft)
        , m_step(static_cast<uint32_t>(uint64_t(fs) * 0x1000000ull / ft / m_sourceDivide))
    {
    }

    uint32_t MameResamplerLofi::historySize() const
    {
        return 5 * m_sourceDivide + m_fs / m_ft + 1;
    }

    int64_t MameResamplerLofi::minSourceIndexForOutput(const uint64_t destSample) const
    {
        const uint64_t seconds = destSample / m_ft;
        const uint64_t dsamp = destSample % m_ft;
        const uint64_t ssamp = (dsamp * m_fs * 0x1000ull) / m_ft;
        int64_t ssample = static_cast<int64_t>((ssamp >> 12) + uint64_t(m_fs) * seconds);
        ssample -= static_cast<int64_t>(4 * m_sourceDivide);
        return ssample;
    }

    int64_t MameResamplerLofi::maxSourceIndexNeeded(const uint64_t destSample, const uint32_t samples) const
    {
        const uint64_t seconds = destSample / m_ft;
        const uint64_t dsamp = destSample % m_ft;
        const uint64_t ssamp = (dsamp * m_fs * 0x1000ull) / m_ft;
        int64_t ssample = static_cast<int64_t>((ssamp >> 12) + uint64_t(m_fs) * seconds);
        uint32_t phase = static_cast<uint32_t>(ssamp & 0xfff);

        if (m_sourceDivide > 1)
        {
            const uint32_t delta = static_cast<uint32_t>(ssample % static_cast<int64_t>(m_sourceDivide));
            phase = (phase | (delta << 12)) / m_sourceDivide;
            ssample -= delta;
        }

        ssample -= static_cast<int64_t>(4 * m_sourceDivide);
        int64_t readPos = ssample;
        int64_t maxUsed = -1;

        auto reader = [&]()
        {
            maxUsed = std::max(maxUsed, readPos + static_cast<int64_t>(m_sourceDivide) - 1);
            readPos += m_sourceDivide;
        };

        phase <<= 12;

        reader();
        reader();
        reader();
        reader();

        for (uint32_t sample = 0; sample != samples; ++sample)
        {
            phase += m_step;
            if (phase & 0x1000000)
            {
                phase &= 0x00ffffff;
                reader();
            }
        }

        return maxUsed;
    }

    void MameResamplerLofi::apply(const std::deque<float>& src, const int64_t srcBase, float* dest, const uint64_t destSample, const uint32_t samples, const float gain) const
    {
        if (samples == 0)
            return;

        const uint64_t seconds = destSample / m_ft;
        const uint64_t dsamp = destSample % m_ft;
        const uint64_t ssamp = (dsamp * m_fs * 0x1000ull) / m_ft;
        int64_t ssample = static_cast<int64_t>((ssamp >> 12) + uint64_t(m_fs) * seconds);
        uint32_t phase = static_cast<uint32_t>(ssamp & 0xfff);

        if (m_sourceDivide > 1)
        {
            const uint32_t delta = static_cast<uint32_t>(ssample % static_cast<int64_t>(m_sourceDivide));
            phase = (phase | (delta << 12)) / m_sourceDivide;
            ssample -= delta;
        }

        ssample -= static_cast<int64_t>(4 * m_sourceDivide);

        // Copy needed source range to contiguous buffer
        const int64_t rangeStart = ssample;
        const int64_t rangeEnd = maxSourceIndexNeeded(destSample, samples);
        const size_t rangeSize = static_cast<size_t>(rangeEnd - rangeStart + 1);

        m_scratchBuffer.resize(rangeSize);
        for (size_t i = 0; i < rangeSize; ++i)
            m_scratchBuffer[i] = sample_at(src, srcBase, rangeStart + static_cast<int64_t>(i));

        int64_t readPos = ssample;

        auto reader = [&]() -> float
        {
            float sm = 0.0f;
            const size_t off = static_cast<size_t>(readPos - rangeStart);
            for (uint32_t i = 0; i != m_sourceDivide; ++i)
                sm += m_scratchBuffer[off + i];
            readPos += m_sourceDivide;
            return sm * m_invSourceDivide;
        };

        phase <<= 12;

        float s0 = reader();
        float s1 = reader();
        float s2 = reader();
        float s3 = reader();

        for (uint32_t sample = 0; sample != samples; ++sample)
        {
            const uint32_t cphase = phase >> 12;
            dest[sample] += gain * (-s0 * s_interpolationTable[0][0x1000 - cphase] + s1 * s_interpolationTable[1][0x1000 - cphase] + s2 * s_interpolationTable[1][cphase] - s3 * s_interpolationTable[0][cphase]);

            phase += m_step;
            if (phase & 0x1000000)
            {
                phase &= 0x00ffffff;
                s0 = s1;
                s1 = s2;
                s2 = s3;
                s3 = reader();
            }
        }
    }
}
