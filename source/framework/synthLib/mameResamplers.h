#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace synthLib
{
    enum class MameResamplerMode : uint8_t
    {
        Hq,
        Lofi
    };

    class MameResampler
    {
    public:
        virtual ~MameResampler() = default;

        virtual uint32_t historySize() const = 0;
        virtual int64_t minSourceIndexForOutput(uint64_t destSample) const = 0;
        virtual int64_t maxSourceIndexNeeded(uint64_t destSample, uint32_t samples) const = 0;
        virtual void apply(const std::deque<float>& src, int64_t srcBase, float* dest, uint64_t destSample, uint32_t samples, float gain) const = 0;

        static std::unique_ptr<MameResampler> create(MameResamplerMode mode, uint32_t fs, uint32_t ft);
    };

    class MameResamplerHq final : public MameResampler
    {
    public:
        MameResamplerHq(uint32_t fs, uint32_t ft, float latency = 0.005f, uint32_t maxOrderPerLane = 400, uint32_t maxLanes = 256);

        uint32_t historySize() const override;
        int64_t minSourceIndexForOutput(uint64_t destSample) const override;
        int64_t maxSourceIndexNeeded(uint64_t destSample, uint32_t samples) const override;
        void apply(const std::deque<float>& src, int64_t srcBase, float* dest, uint64_t destSample, uint32_t samples, float gain) const override;

    private:
        static uint32_t computeGcd(uint32_t fs, uint32_t ft);

        uint32_t m_orderPerLane = 0;
        uint32_t m_ftm = 0;
        uint32_t m_fsm = 0;
        uint32_t m_ft = 0;
        uint32_t m_fs = 0;
        uint32_t m_delta = 0;
        uint32_t m_skip = 0;
        uint32_t m_phases = 0;
        uint32_t m_phaseShift = 0;

        std::vector<std::vector<float>> m_coefficients;
        mutable std::vector<float> m_scratchBuffer;
    };

    class MameResamplerLofi final : public MameResampler
    {
    public:
        MameResamplerLofi(uint32_t fs, uint32_t ft);

        uint32_t historySize() const override;
        int64_t minSourceIndexForOutput(uint64_t destSample) const override;
        int64_t maxSourceIndexNeeded(uint64_t destSample, uint32_t samples) const override;
        void apply(const std::deque<float>& src, int64_t srcBase, float* dest, uint64_t destSample, uint32_t samples, float gain) const override;

    private:
        static const std::array<std::array<float, 0x1001>, 2> s_interpolationTable;

        uint32_t m_sourceDivide = 1;
        float m_invSourceDivide = 1.0f;
        uint32_t m_fs = 0;
        uint32_t m_ft = 0;
        uint32_t m_step = 0;
        mutable std::vector<float> m_scratchBuffer;
    };
}

