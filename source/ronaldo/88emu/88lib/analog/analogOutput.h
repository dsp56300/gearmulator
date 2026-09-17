#pragma once

#include "88lib/deviceModel.h"
#include "synthLib/analog/analogFilter.h"
#include "synthLib/dac.h"

#include <cstdint>
#include <vector>

namespace emu88Lib
{

    // The player-wide choice, applied to whichever board runs: no circuit, the board's own
    // circuit (Auto), or one specific circuit. Persisted: append new entries, never reorder.
    enum class AnalogOutputMode : uint8_t
    {
        Off = 0,
        Auto,
        Cm32p,
        Sc88,
        Sc88Vl,
        Sc88Pro,
        Sc55Mk1,
        Scc1,
        G800,
        Sc8850,
        Sc8820,
        Sc55Mk2,
        Cm32l,
        Cm64,
    };

    // A modelled output circuit. None passes the DAC words through unchanged.
    enum class AnalogModel : uint8_t
    {
        None = 0,
        Cm32p,
        Sc88,
        Sc88Vl,
        Sc88Pro,
        Sc55Mk1,
        Scc1,
        G800,
        Sc8850,
        Sc8820,
        Sc55Mk2,
        Cm32l,
        Cm64,
    };

    // Off, Auto, then every modelled circuit, in menu order.
    const std::vector<AnalogOutputMode>& getAnalogOutputModes();
    bool isAnalogOutputModeValue(uint32_t _value);
    const char* getAnalogOutputModeName(AnalogOutputMode _mode);
    const char* getAnalogModelName(AnalogModel _model);

    // What a board's own output amplifiers do to its level, after its DAC and past the
    // filters, which are all unity at DC. This belongs to the board rather than to the
    // circuit model: it applies whether or not the circuit is being emulated, so turning
    // analog emulation off changes the tone and leaves the volume alone. A board that sums
    // two paths reports one gain for each. Both are 1 where a board's output stage has not
    // been read off the schematic, which is every board but the CM pair.
    struct BoardOutputGain
    {
        float a = 1.0f;
        float b = 1.0f;
    };

    BoardOutputGain getBoardOutputGain(DeviceModel _device);

    // The circuit Auto selects for a board: None until that board's output stage is modelled.
    AnalogModel getAutoAnalogModel(DeviceModel _device);
    AnalogModel resolveAnalogModel(AnalogOutputMode _mode, DeviceModel _device);

    // The word width of a board's DAC. Words on the shared 24-bit interface carry more bits
    // than most DACs convert; the surplus low bits are dropped as the DAC does.
    uint8_t getDacBits(DeviceModel _device);


    // Output samples per DAC frame. Holding each frame for several output samples lets the
    // sample-and-hold images reach the circuit, as they do on the board. A held frame differs
    // from the board's continuous hold by a sinc at the output rate: 0.06 dB at 16 kHz with 8x.
    uint32_t getAnalogOversampling(AnalogModel _model);

    // Runs the selected circuit. Selecting one allocates it, so switch models outside process().
    class AnalogOutput
    {
    public:
        AnalogOutput();
        ~AnalogOutput();

        void setModel(AnalogModel _model, float _dacSamplerate);
        AnalogModel model() const { return m_model; }
        uint32_t oversampling() const { return m_oversampling; }
        void reset();

        // Filters one output sample of the held DAC frame in place; None leaves it unchanged.
        void process(float& _left, float& _right);

        // A board that is two boards summed - the CM-64, whose LA half hands its finished line
        // output to the PCM half's mixer - has each half filtered by its own circuit before
        // they meet, and the mixer and everything after it in common. Sums into _left/_right
        // whatever the selected model does or does not model, so a board wired this way can
        // always call it.
        void processSplit(float& _left, float& _right, float _leftB, float _rightB);

    private:
        AnalogModel m_model = AnalogModel::None;
        uint32_t m_oversampling = 1;
        // The shared circuit; m_circuitA and m_circuitB are the per-half ones in front of it,
        // empty unless the selected model names them.
        synthLib::OutputChain m_circuit;
        synthLib::OutputChain m_circuitA;
        synthLib::OutputChain m_circuitB;

    };
} // namespace emu88Lib
