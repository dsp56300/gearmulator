#include "88lib/analog/analogOutput.h"
#include "88lib/analog/analogModels.h"


#include <array>
#include <utility>

namespace emu88Lib
{
    namespace
    {
        // The resistor-only estimate made PCM 5.3 dB hotter, but omitted the DAC/
        // reference and operating-point scale. The hardware capture supports only
        // about 0.33 dB averaged over L/R. Preserve the previous LA left output level
        // as common normalization: a recording at 75% knob cannot establish volts/FS.
        // Trims match the measured CM unit directly. The Korg reference's -0.108 dB
        // R/L is not an independently measured ADC imbalance and is NOT removed.
        // Fixed-gain line inputs still leave a small unmeasured channel-tolerance
        // uncertainty; these trims are not a claim about every CM module.
        constexpr double g_cm32lGain = (4.7 / 2.2) * 1.5 * (6.8 / (4.7 + 1.5 + 6.8));
        constexpr double g_cm32pGain = g_cm32lGain * 1.0271;
        constexpr float g_cm32lRight = 0.9793f;
        constexpr float g_cm32pRight = 1.0011f;

        struct ModelInfo
        {
            AnalogModel model;
            AnalogOutputMode mode;
            const char* name;
            uint32_t oversampling;
            // The circuit the signal passes. On a board that sums two separately filtered
            // paths this is only the shared part, after they meet; configureA and configureB
            // are then what each half passes on its way there.
            void (*configure)(synthLib::OutputChain&, double);
            void (*configureA)(synthLib::OutputChain&, double) = nullptr;
            void (*configureB)(synthLib::OutputChain&, double) = nullptr;
        };

        const std::array<ModelInfo, 12> g_models{{
            {AnalogModel::Cm32l, AnalogOutputMode::Cm32l, "CM-32L", 8, &configureCm32l},
            // The LA board's whole chain and the PCM board's reconstruction filter meet at the
            // mixer; everything from there on is shared.
            {AnalogModel::Cm64, AnalogOutputMode::Cm64, "CM-64", 8, &configureCmMixerOutput,
             &configureCm32l, &configureCm32pReconstruction},
            {AnalogModel::Cm32p, AnalogOutputMode::Cm32p, "CM-32P", 8, &configureCm32p},
            {AnalogModel::Sc88, AnalogOutputMode::Sc88, "SC-88", 8, &configureSc88},
            {AnalogModel::Sc88Vl, AnalogOutputMode::Sc88Vl, "SC-88VL", 8, &configureSc88Vl},
            {AnalogModel::Sc88Pro, AnalogOutputMode::Sc88Pro, "SC-88Pro", 8, &configureSc88Pro},
            {AnalogModel::Sc55Mk1, AnalogOutputMode::Sc55Mk1, "SC-55", 4, &configureSc55Mk1}, // 64 kHz DAC
            {AnalogModel::Scc1, AnalogOutputMode::Scc1, "SCC-1", 4, &configureScc1}, // 64 kHz DAC
            {AnalogModel::G800, AnalogOutputMode::G800, "G-800", 8, &configureG800},
            {AnalogModel::Sc8850, AnalogOutputMode::Sc8850, "SC-8850", 8, &configureSc8850},
            {AnalogModel::Sc8820, AnalogOutputMode::Sc8820, "SC-8820", 8, &configureSc8820},
            {AnalogModel::Sc55Mk2, AnalogOutputMode::Sc55Mk2, "SC-55mkII", 4, &configureSc55Mk2}, // 66.2 kHz DAC
        }};

        // Boards whose output circuit has been modelled. Auto leaves every other board digital.
        constexpr std::array<std::pair<DeviceModel, AnalogModel>, 19> g_autoModels{{
            {DeviceModel::Cm32p, AnalogModel::Cm32p},
            {DeviceModel::Cm32l, AnalogModel::Cm32l},
            // The CM-64 is both boards at once: each half through its own reconstruction
            // filter, then the CM-32P's mixer and output network, which is where the LA
            // board's line output arrives. AnalogOutput::processSplit() runs the three.
            {DeviceModel::Cm64, AnalogModel::Cm64},
            {DeviceModel::Sc88, AnalogModel::Sc88},
            {DeviceModel::Sc88VL, AnalogModel::Sc88Vl},
            {DeviceModel::Sc88Pro, AnalogModel::Sc88Pro},
            {DeviceModel::Sc55Mk1, AnalogModel::Sc55Mk1},
            {DeviceModel::Cm300,
             AnalogModel::Sc55Mk1}, // the module shares the SC-55 output stage; the SCC-1 card is a manual pick
            {DeviceModel::Scc1a, AnalogModel::Scc1}, // only ever a card: the SCC-1 card's output stage
            {DeviceModel::Sc155, AnalogModel::Sc55Mk1}, // assumed to share the SC-55 board
            {DeviceModel::Xpgs, AnalogModel::G800},
            {DeviceModel::Sc8850, AnalogModel::Sc8850},
            {DeviceModel::Sc8820, AnalogModel::Sc8820},
            {DeviceModel::VeGsPro, AnalogModel::Sc88Pro}, // same output board as the SC-88Pro
            // The mkII generation: only the SC-55mkII board itself has been read.
            {DeviceModel::Sc55Mk2, AnalogModel::Sc55Mk2},
            {DeviceModel::Sc55St, AnalogModel::Sc55Mk2},
            {DeviceModel::Sc155Mk2, AnalogModel::Sc55Mk2},
            {DeviceModel::Scb55, AnalogModel::Sc55Mk2},
            {DeviceModel::Rlp3237, AnalogModel::Sc55Mk2},
        }};

        const ModelInfo* findModel(const AnalogModel _model)
        {
            for (const auto& info : g_models)
            {
                if (info.model == _model)
                    return &info;
            }
            return nullptr;
        }

        const ModelInfo* findMode(const AnalogOutputMode _mode)
        {
            for (const auto& info : g_models)
            {
                if (info.mode == _mode)
                    return &info;
            }
            return nullptr;
        }
    } // namespace

    const std::vector<AnalogOutputMode>& getAnalogOutputModes()
    {
        static const std::vector<AnalogOutputMode> modes = []
        {
            std::vector<AnalogOutputMode> result{AnalogOutputMode::Off, AnalogOutputMode::Auto};
            for (const auto& info : g_models)
                result.push_back(info.mode);
            return result;
        }();
        return modes;
    }

    bool isAnalogOutputModeValue(const uint32_t _value)
    {
        for (const auto mode : getAnalogOutputModes())
        {
            if (static_cast<uint32_t>(mode) == _value)
                return true;
        }
        return false;
    }

    const char* getAnalogOutputModeName(const AnalogOutputMode _mode)
    {
        if (_mode == AnalogOutputMode::Auto)
            return "Auto";
        const auto* info = findMode(_mode);
        return info ? info->name : "Off";
    }

    const char* getAnalogModelName(const AnalogModel _model)
    {
        const auto* info = findModel(_model);
        return info ? info->name : "Off";
    }

    BoardOutputGain getBoardOutputGain(const DeviceModel _device)
    {
        switch (_device)
        {
        case DeviceModel::Cm32l:
            return {static_cast<float>(g_cm32lGain), 1.0f, g_cm32lRight};
        case DeviceModel::Cm32p:
            return {static_cast<float>(g_cm32pGain), 1.0f, g_cm32pRight};
        // The LA board's line output is the first path and the PCM board's the second, the
        // order HardwareDevice hands them over in.
        case DeviceModel::Cm64:
            return {static_cast<float>(g_cm32lGain), static_cast<float>(g_cm32pGain), g_cm32lRight, g_cm32pRight};
        default:
            return {};
        }
    }

    AnalogModel getAutoAnalogModel(const DeviceModel _device)
    {
        for (const auto& [device, model] : g_autoModels)
        {
            if (device == _device)
                return model;
        }
        return AnalogModel::None;
    }

    AnalogModel resolveAnalogModel(const AnalogOutputMode _mode, const DeviceModel _device)
    {
        if (_mode == AnalogOutputMode::Auto)
            return getAutoAnalogModel(_device);
        const auto* info = findMode(_mode);
        return info ? info->model : AnalogModel::None;
    }

    uint8_t getDacBits(const DeviceModel _device)
    {
        switch (_device)
        {
        case DeviceModel::Cm32l: // PCM54HP
        case DeviceModel::Cm32p: // PCM56P
        case DeviceModel::Cm64: // Both halves use 16-bit DACs, before analog mixing/VCA.
        case DeviceModel::Sc55Mk1: // uPD6376, and the boards that share it
        case DeviceModel::Cm300:
        case DeviceModel::Scc1a:
        case DeviceModel::Sc155:
            return 16;
        case DeviceModel::Sc8850:
        case DeviceModel::Sc8820:
            return 24;
        default: // PCM69AU, uPD63200 and the mkII family
            return 18;
        }
    }

    uint32_t getAnalogOversampling(const AnalogModel _model)
    {
        const auto* info = findModel(_model);
        return info ? info->oversampling : 1;
    }

    AnalogOutput::AnalogOutput() = default;
    AnalogOutput::~AnalogOutput() = default;

    void AnalogOutput::setModel(const AnalogModel _model, const float _dacSamplerate)
    {
        const auto* info = findModel(_model);
        m_model = info ? _model : AnalogModel::None;
        m_oversampling = info ? info->oversampling : 1;
        m_circuit = {};
        m_circuitA = {};
        m_circuitB = {};
        if (!info)
            return;
        const auto rate = static_cast<double>(_dacSamplerate) * m_oversampling;
        info->configure(m_circuit, rate);
        if (info->configureA)
            info->configureA(m_circuitA, rate);
        if (info->configureB)
            info->configureB(m_circuitB, rate);
    }

    void AnalogOutput::reset()
    {
        m_circuit.reset();
        m_circuitA.reset();
        m_circuitB.reset();
    }

    void AnalogOutput::process(float& _left, float& _right) { m_circuit.process(_left, _right); }

    void AnalogOutput::processSplit(float& _left, float& _right, float _leftB, float _rightB)
    {
        m_circuitA.process(_left, _right);
        m_circuitB.process(_leftB, _rightB);
        _left += _leftB;
        _right += _rightB;
        m_circuit.process(_left, _right);
    }
} // namespace emu88Lib
