#include "Virus_LfoEditor.h"
#include "BinaryData.h"
#include "Ui_Utils.h"

using namespace juce;
constexpr auto comboBoxWidth = 98;

LfoEditor::LfoEditor()
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

LfoEditor::LfoBase::LfoBase()
{
    for (auto *s : {&m_rate, &m_keytrack, &m_amount})
        setupRotary(*this, *s);
    addAndMakeVisible(m_subWaveform);
    m_subWaveform.setBounds(8, 123, Buttons::HandleButton::kWidth, Buttons::HandleButton::kHeight);

    addAndMakeVisible(m_shape);
    m_shape.setBounds(10, 37, 84, comboBoxHeight);
    addAndMakeVisible(m_clock);
    m_clock.setBounds(10, 80, 84, comboBoxHeight);
    addAndMakeVisible(m_assignDest);
}

LfoEditor::LfoTwoOneShared::LfoTwoOneShared() : m_link(false)
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
}

LfoEditor::LfoOne::LfoOne()
{
    for (auto *s : {&m_osc1Pitch, &m_osc2Pitch, &m_filterGain, &m_pw12, &m_reso12})
        setupRotary(*this, *s);
    m_osc1Pitch.setBounds(245, m_keytrack.getY(), knobSize, knobSize);
    m_osc2Pitch.setBounds(m_osc1Pitch.getBounds().translated(64, 0));
    m_filterGain.setBounds(m_osc1Pitch.getBounds().withY(m_amount.getY()));
    m_pw12.setBounds(374, 14, knobSize, knobSize);
    m_reso12.setBounds(m_pw12.getBounds().translated(knobSize - 4, 0));
    addAndMakeVisible(m_link); // add last to allow clicking over knobs area...
}

LfoEditor::LfoTwo::LfoTwo()
{
    for (auto *s : {&m_f1cutoff, &m_f2cutoff, &m_panning, &m_shape12, &m_fmAmount})
        setupRotary(*this, *s);
    m_f1cutoff.setBounds(245, m_keytrack.getY(), knobSize, knobSize);
    m_f2cutoff.setBounds(m_f1cutoff.getBounds().translated(64, 0));
    m_panning.setBounds(m_f1cutoff.getBounds().withY(m_amount.getY()));
    m_shape12.setBounds(374, 14, knobSize, knobSize);
    m_fmAmount.setBounds(m_shape12.getBounds().translated(knobSize - 4, 0));
    addAndMakeVisible(m_link); // add last to allow clicking over knobs area...
}

LfoEditor::LfoThree::LfoThree()
{
    setupRotary(*this, m_fadeIn);
    m_rate.setBounds(107, 22, knobSize, knobSize);
    m_fadeIn.setBounds(m_rate.getBounds().translated(knobSize - 6, 0));
    m_keytrack.setBounds(m_rate.getBounds().translated(0, knobSize + 6));
    m_amount.setBounds(307, 22, knobSize, knobSize);
    m_assignDest.setBounds(393, 45, comboBoxWidth, comboBoxHeight);
}

LfoEditor::ModMatrix::ModMatrix()
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
