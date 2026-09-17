#include "88lib/analog/analogOutput.h"
#include "88lib/analog/analogModels.h"


#include <array>
#include <utility>

namespace emu88Lib
{
    namespace
    {
        // What each CM board's amplifiers do to its level. The low-passes are unity at DC on
        // both - the Sallen-Keys are followers, the multiple-feedback sections have rf = r1 -
        // so all of it sits after them, and it is the whole reason the two halves of a CM-64
        // balance: the PCM board arrives 5.3 dB hotter than the LA board.
        //
        // It is the board's, not the circuit model's, so getBoardOutputGain() hands it to the
        // device and it survives the analog emulation being switched off.
        //
        //   CM-32P  I/V R47A 6.8k / R46A 2.2k                             = 3.091
        //           then R48A 100k into the mixer's R49A 100k, at unity.
        //   CM-32L  I/V R63 4.7k / R51 2.2k                               = 2.136
        //           IC22a R57 15k / R56 10k                               x 1.5
        //           output divider R49 6.8k / (R46 4.7k + R48 1.5k + R49)  x 0.523
        //           then R48C 100k into the same mixer, at unity.         = 1.676
        //
        // Both feed the same M5207L01 through the same 2.2k, so its transconductance cancels
        // out of the ratio; taking it as unity is what makes the two absolute figures, and it
        // is the one number here that is assumed rather than read off the board.
        constexpr double g_cm32lGain = (4.7 / 2.2) * 1.5 * (6.8 / (4.7 + 1.5 + 6.8));
        constexpr double g_cm32pGain = 6.8 / 2.2;

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
            return {static_cast<float>(g_cm32lGain), 1.0f};
        case DeviceModel::Cm32p:
            return {static_cast<float>(g_cm32pGain), 1.0f};
        // The LA board's line output is the first path and the PCM board's the second, the
        // order HardwareDevice hands them over in.
        case DeviceModel::Cm64:
            return {static_cast<float>(g_cm32lGain), static_cast<float>(g_cm32pGain)};
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
