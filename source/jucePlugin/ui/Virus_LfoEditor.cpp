#include "Virus_LfoEditor.h"
#include "BinaryData.h"
#include "Ui_Utils.h"
#include "../VirusParameterBinding.h"

using namespace juce;
constexpr auto comboBoxWidth = 98;

LfoEditor::LfoEditor(VirusParameterBinding& _parameterBinding) : m_lfoOne(_parameterBinding), m_lfoTwo(_parameterBinding), m_lfoThree(_parameterBinding), m_modMatrix(_parameterBinding)
{
    setupBackground(*this, m_background, BinaryData::bg_lfo_1018x620_png, BinaryData::bg_lfo_1018x620_pngSize);
    setBounds(m_background->getDrawableBounds().toNearestIntEdges());

    m_lfoOne.setBounds(23, 28, 522, 188);
    addAndMakeVisible(m_lfoOne);
    m_lfoTwo.setBounds(m_lfoOne.getBounds().withY(m_lfoOne.getBottom() + 2));
    addAndMakeVisible(m_lfoTwo);
    m_lfoThree.setBounds(m_lfoTwo.getBounds().withY(m_lfoTwo.getBottom() + 2));
    addAndMakeVisible(m_lfoThree);
    constexpr auto kMatrixWidth = 450;
    constexpr auto kMatrixHeight = 568;
    m_modMatrix.setBounds(getWidth() - kMatrixWidth - 12, 28, kMatrixWidth, kMatrixHeight);
    addAndMakeVisible(m_modMatrix);
}

LfoEditor::LfoBase::LfoBase(VirusParameterBinding& _parameterBinding, uint8_t _lfoIndex)
{
    for (auto *s : {&m_rate, &m_keytrack, &m_amount})
        setupRotary(*this, *s);
    addAndMakeVisible(m_mode);
    m_mode.setBounds(8, 123, Buttons::HandleButton::kWidth, Buttons::HandleButton::kHeight);

    addAndMakeVisible(m_shape);
    m_shape.setBounds(10, 37, 84, comboBoxHeight);
    addAndMakeVisible(m_clock);
    m_clock.setBounds(10, 80, 84, comboBoxHeight);
    addAndMakeVisible(m_assignDest);
    const Virus::ParameterType rate[] = {Virus::Param_Lfo1Rate, Virus::Param_Lfo2Rate, Virus::Param_Lfo3Rate};
    const Virus::ParameterType keytrack[] = {Virus::Param_Lfo1Keyfollow, Virus::Param_Lfo2Keyfollow,
                                             Virus::Param_Lfo3Keyfollow};
    const Virus::ParameterType amount[] = {Virus::Param_Lfo1AssignAmount, Virus::Param_Lfo2AssignAmount,
                                           Virus::Param_OscLfo3Amount};
    const Virus::ParameterType shapes[] = {Virus::Param_Lfo1Shape, Virus::Param_Lfo2Shape, Virus::Param_Lfo3Shape};
    const Virus::ParameterType clock[] = {Virus::Param_Lfo1Clock, Virus::Param_Lfo2Clock, Virus::Param_Lfo3Clock};
    const Virus::ParameterType assignDest[] = {Virus::Param_Lfo1AssignDest, Virus::Param_Lfo2AssignDest,
                                               Virus::Param_Lfo3Destination};
    const Virus::ParameterType lfoModes[] = {Virus::Param_Lfo1Mode, Virus::Param_Lfo2Mode, Virus::Param_Lfo3Mode};

    _parameterBinding.bind(m_rate, rate[_lfoIndex]);
    _parameterBinding.bind(m_keytrack, keytrack[_lfoIndex]);
    _parameterBinding.bind(m_amount, amount[_lfoIndex]);
    _parameterBinding.bind(m_shape, shapes[_lfoIndex]);
    _parameterBinding.bind(m_clock, clock[_lfoIndex]);
    _parameterBinding.bind(m_assignDest, assignDest[_lfoIndex]);
    _parameterBinding.bind(m_mode, lfoModes[_lfoIndex]);
}

LfoEditor::LfoTwoOneShared::LfoTwoOneShared(VirusParameterBinding& _parameterBinding, uint8_t _lfoIndex) : LfoBase(_parameterBinding, _lfoIndex), m_link(false)
{
    for (auto *s : {&m_contour, &m_phase})
        setupRotary(*this, *s);
    m_rate.setBounds(106, 15, knobSize, knobSize);
    m_keytrack.setBounds(m_rate.getBounds().translated(65, 0));
    m_contour.setBounds(m_rate.getBounds().translated(0, knobSize + 14));
    m_phase.setBounds(m_keytrack.getBounds().translated(0, knobSize + 14));
    m_amount.setBounds(307, knobSize + 28, knobSize, knobSize);
    addAndMakeVisible(m_envMode);
    m_envMode.setBounds(66, 122, Buttons::LfoButton::kWidth, Buttons::LfoButton::kHeight);

    m_link.setBounds(293, 8, 36, 12);
    m_assignDest.setBounds(393, 122, comboBoxWidth, comboBoxHeight);

    //_parameterBinding.bind(m_rate, Virus::Param_Lfo1Rate);
    //_parameterBinding.bind(m_keytrack, Virus::Param_Lfo1Keyfollow);
    _parameterBinding.bind(m_contour, _lfoIndex == 0 ? Virus::Param_Lfo1Symmetry : Virus::Param_Lfo2Symmetry);
    _parameterBinding.bind(m_phase, _lfoIndex == 0 ? Virus::Param_Lfo1KeyTrigger : Virus::Param_Lfo2Keytrigger);
    //parameterBinding.bind(m_amount, Virus::Param_Lfo1AssignAmount);
    _parameterBinding.bind(m_envMode, _lfoIndex == 0 ? Virus::Param_Lfo1EnvMode : Virus::Param_Lfo2EnvMode);
    //_parameterBinding.bind(m_link, _lfoIndex == 0 ? Virus::Param_Lfo1Mode : Virus::Param_Lfo2Mode);
    //_parameterBinding.bind(m_assignDest, Virus::Param_Lfo1AssignDest);
}

LfoEditor::LfoOne::LfoOne(VirusParameterBinding& _parameterBinding) : LfoTwoOneShared(_parameterBinding, 0)
{
    for (auto *s : {&m_osc1Pitch, &m_osc2Pitch, &m_filterGain, &m_pw12, &m_reso12})
        setupRotary(*this, *s);
    m_osc1Pitch.setBounds(245, m_keytrack.getY(), knobSize, knobSize);
    m_osc2Pitch.setBounds(m_osc1Pitch.getBounds().translated(64, 0));
    m_filterGain.setBounds(m_osc1Pitch.getBounds().withY(m_amount.getY()));
    m_pw12.setBounds(374, 14, knobSize, knobSize);
    m_reso12.setBounds(m_pw12.getBounds().translated(knobSize - 4, 0));
    addAndMakeVisible(m_link); // add last to allow clicking over knobs area...

    _parameterBinding.bind(m_osc1Pitch, Virus::Param_Osc1Lfo1Amount);
    _parameterBinding.bind(m_osc2Pitch, Virus::Param_Osc2Lfo1Amount);
    _parameterBinding.bind(m_filterGain, Virus::Param_FltGainLfo1Amount);
    _parameterBinding.bind(m_pw12, Virus::Param_PWLfo1Amount);
    _parameterBinding.bind(m_reso12, Virus::Param_ResoLfo1Amount);
}

LfoEditor::LfoTwo::LfoTwo(VirusParameterBinding& _parameterBinding) : LfoTwoOneShared(_parameterBinding, 1)
{
    for (auto *s : {&m_f1cutoff, &m_f2cutoff, &m_panning, &m_shape12, &m_fmAmount})
        setupRotary(*this, *s);
    m_f1cutoff.setBounds(245, m_keytrack.getY(), knobSize, knobSize);
    m_f2cutoff.setBounds(m_f1cutoff.getBounds().translated(64, 0));
    m_panning.setBounds(m_f1cutoff.getBounds().withY(m_amount.getY()));
    m_shape12.setBounds(374, 14, knobSize, knobSize);
    m_fmAmount.setBounds(m_shape12.getBounds().translated(knobSize - 4, 0));
    addAndMakeVisible(m_link); // add last to allow clicking over knobs area...

    _parameterBinding.bind(m_f1cutoff, Virus::Param_Cutoff1Lfo2Amount);
    _parameterBinding.bind(m_f2cutoff, Virus::Param_Cutoff2Lfo2Amount);
    _parameterBinding.bind(m_panning, Virus::Param_PanoramaLfo2Amount);
    _parameterBinding.bind(m_shape12, Virus::Param_OscShapeLfo2Amount);
    _parameterBinding.bind(m_fmAmount, Virus::Param_FmAmountLfo2Amount);
}

LfoEditor::LfoThree::LfoThree(VirusParameterBinding& _parameterBinding) : LfoBase(_parameterBinding, 2)
{
    setupRotary(*this, m_fadeIn);
    m_rate.setBounds(107, 22, knobSize, knobSize);
    m_fadeIn.setBounds(m_rate.getBounds().translated(knobSize - 6, 0));
    m_keytrack.setBounds(m_rate.getBounds().translated(0, knobSize + 6));
    m_amount.setBounds(307, 22, knobSize, knobSize);
    m_assignDest.setBounds(393, 45, comboBoxWidth, comboBoxHeight);

    _parameterBinding.bind(m_fadeIn, Virus::Param_Lfo3FadeInTime);
}

LfoEditor::ModMatrix::ModMatrix(VirusParameterBinding& _parameterBinding)
{
    constexpr auto kNumOfSlots = 6;
    for (auto s = 0; s < kNumOfSlots; s++)
        m_modMatrix.push_back(std::make_unique<MatrixSlot>(s == 0 ? 3 : s == 1 ? 2 : 1));
    setupSlot(0, {{20, 89}, {20, 165}, {20, 241}}, {86, 65});
    setupSlot(1, {{20, 386}, {20, 462}}, {86, 363});
    setupSlot(2, {{255, 89}}, {320, 65});
    setupSlot(3, {{255, 214}}, {320, 190});
    setupSlot(4, {{255, 338}}, {320, 314});
    setupSlot(5, {{255, 462}}, {320, 439});

    // slot 0 is assign2, slot 1 is assign3, then 1,4,5,6
    _parameterBinding.bind(m_modMatrix[0]->m_source, Virus::Param_Assign2Source);
    _parameterBinding.bind(m_modMatrix[1]->m_source, Virus::Param_Assign3Source);
    _parameterBinding.bind(m_modMatrix[2]->m_source, Virus::Param_Assign1Source);
    _parameterBinding.bind(m_modMatrix[3]->m_source, Virus::Param_Assign4Source);
    _parameterBinding.bind(m_modMatrix[4]->m_source, Virus::Param_Assign5Source);
    _parameterBinding.bind(m_modMatrix[5]->m_source, Virus::Param_Assign6Source);

    _parameterBinding.bind(m_modMatrix[0]->m_destinations[0]->m_amount, Virus::Param_Assign2Amount1);
    _parameterBinding.bind(m_modMatrix[0]->m_destinations[1]->m_amount, Virus::Param_Assign2Amount2);
    _parameterBinding.bind(m_modMatrix[0]->m_destinations[2]->m_amount, Virus::Param_Assign2Amount3);

    _parameterBinding.bind(m_modMatrix[1]->m_destinations[0]->m_amount, Virus::Param_Assign3Amount1);
    _parameterBinding.bind(m_modMatrix[1]->m_destinations[1]->m_amount, Virus::Param_Assign3Amount2);

    _parameterBinding.bind(m_modMatrix[2]->m_destinations[0]->m_amount, Virus::Param_Assign1Amount);

    _parameterBinding.bind(m_modMatrix[3]->m_destinations[0]->m_amount, Virus::Param_Assign4Amount);
    _parameterBinding.bind(m_modMatrix[4]->m_destinations[0]->m_amount, Virus::Param_Assign5Amount);
    _parameterBinding.bind(m_modMatrix[5]->m_destinations[0]->m_amount, Virus::Param_Assign6Amount);

    _parameterBinding.bind(m_modMatrix[0]->m_destinations[0]->m_dest, Virus::Param_Assign2Destination1);
    _parameterBinding.bind(m_modMatrix[0]->m_destinations[1]->m_dest, Virus::Param_Assign2Destination2);
    _parameterBinding.bind(m_modMatrix[0]->m_destinations[2]->m_dest, Virus::Param_Assign2Destination3);
    _parameterBinding.bind(m_modMatrix[1]->m_destinations[0]->m_dest, Virus::Param_Assign3Destination1);
    _parameterBinding.bind(m_modMatrix[1]->m_destinations[1]->m_dest, Virus::Param_Assign3Destination2);

    _parameterBinding.bind(m_modMatrix[2]->m_destinations[0]->m_dest, Virus::Param_Assign1Destination);
    _parameterBinding.bind(m_modMatrix[3]->m_destinations[0]->m_dest, Virus::Param_Assign4Destination);
    _parameterBinding.bind(m_modMatrix[4]->m_destinations[0]->m_dest, Virus::Param_Assign5Destination);
    _parameterBinding.bind(m_modMatrix[5]->m_destinations[0]->m_dest, Virus::Param_Assign6Destination);
}

void LfoEditor::ModMatrix::setupSlot(int slotNum, std::initializer_list<juce::Point<int>> destsPos,
                                     juce::Point<int> sourcePos)
{
    constexpr auto width = MatrixSlot::Dest::kWidth;
    constexpr auto height = MatrixSlot::Dest::kHeight;
    auto &slot = *m_modMatrix[slotNum];
    int i = 0;
    for (auto pos : destsPos)
    {
        auto &dest = *slot.m_destinations[i];
        addAndMakeVisible(dest);
        dest.setBounds(pos.x, pos.y, width, height);
        i++;
    }
    addAndMakeVisible(slot.m_source);
    slot.m_source.setBounds(sourcePos.x, sourcePos.y, comboBoxWidth, comboBoxHeight);
}

LfoEditor::ModMatrix::MatrixSlot::Dest::Dest()
{
    setupRotary(*this, m_amount);
    m_amount.getProperties().set(Virus::LookAndFeel::KnobStyleProp, Virus::LookAndFeel::KnobStyle::GENERIC_BLUE);
    m_amount.setBounds(-6, -4, knobSize, knobSize);
    addAndMakeVisible(m_dest);
    m_dest.setBounds(66, 35, comboBoxWidth, comboBoxHeight);
}

LfoEditor::ModMatrix::MatrixSlot::MatrixSlot(int numOfDests)
{
    for (auto d = 0; d < numOfDests; d++)
        m_destinations.push_back(std::make_unique<Dest>());
}
