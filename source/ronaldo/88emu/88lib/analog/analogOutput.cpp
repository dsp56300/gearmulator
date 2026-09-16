#include "88lib/analog/analogOutput.h"
#include "88lib/analog/analogModels.h"


#include <array>
#include <utility>

namespace emu88Lib
{
    namespace
    {
        struct ModelInfo
        {
            AnalogModel model;
            AnalogOutputMode mode;
            const char* name;
            uint32_t oversampling;
            void (*configure)(synthLib::OutputChain&, double);
        };

        const std::array<ModelInfo, 10> g_models{{
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
        constexpr std::array<std::pair<DeviceModel, AnalogModel>, 17> g_autoModels{{
            {DeviceModel::Cm32p, AnalogModel::Cm32p},
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
        if (info)
            info->configure(m_circuit, static_cast<double>(_dacSamplerate) * m_oversampling);
    }

    void AnalogOutput::reset() { m_circuit.reset(); }

    void AnalogOutput::process(float& _left, float& _right) { m_circuit.process(_left, _right); }
} // namespace emu88Lib
