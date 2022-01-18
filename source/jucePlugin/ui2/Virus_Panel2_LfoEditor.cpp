#include "Virus_Panel2_LfoEditor.h"
#include "BinaryData.h"
#include "Ui_Utils.h"
#include "../VirusParameterBinding.h"

using namespace juce;


LfoEditor::LfoEditor(VirusParameterBinding& _parameterBinding)
{
	setupBackground(*this, m_background, BinaryData::panel_2_png, BinaryData::panel_2_pngSize);
    setBounds(m_background->getDrawableBounds().toNearestIntEdges());

    //LFO 1
    for (auto *s : {&m_lfo1Rate, &m_lfo1Symmetry, &m_lfo1Osc1Pitch, &m_lfo1Osc2Pitch, &m_lfo1Pw12, &m_lfo1Reso12,
				 &m_lfo1KeyFollow, &m_lfo1Keytrigger, &m_lfo1AssignAmount, &m_lfo1FilterGain})
	{
		setupRotary(*this, *s);
	}

	m_lfo1Rate.setBounds(102 - knobSize / 2, 125 - knobSize / 2, knobSize, knobSize);
	m_lfo1Symmetry.setBounds(417 - knobSize / 2, 125 - knobSize / 2, knobSize, knobSize);
	m_lfo1Osc1Pitch.setBounds(102 - knobSize / 2, 313 - knobSize / 2, knobSize, knobSize);
	m_lfo1Osc2Pitch.setBounds(262 - knobSize / 2, 313 - knobSize / 2, knobSize, knobSize);
	m_lfo1Pw12.setBounds(417 - knobSize / 2, 313 - knobSize / 2, knobSize, knobSize);
	m_lfo1Reso12.setBounds(102 - knobSize / 2, 517 - knobSize / 2, knobSize, knobSize);
	m_lfo1KeyFollow.setBounds(258 - knobSize / 2, 517 - knobSize / 2, knobSize, knobSize);
	m_lfo1Keytrigger.setBounds(417 - knobSize / 2, 517 - knobSize / 2, knobSize, knobSize);
	m_lfo1AssignAmount.setBounds(102 - knobSize / 2, 705 - knobSize / 2, knobSize, knobSize);
	m_lfo1FilterGain.setBounds(417 - knobSize / 2, 886 - knobSize / 2, knobSize, knobSize);

	addAndMakeVisible(m_lfo1Clock);
	m_lfo1Clock.setBounds(258+comboBoxXMargin - comboBoxWidth / 2, 84 - comboBoxHeight / 2, comboBoxWidth, comboBoxHeight);
    addAndMakeVisible(m_lfo1Shape);
	m_lfo1Shape.setBounds(258+comboBoxXMargin - comboBoxWidth / 2, 169 - comboBoxHeight / 2, comboBoxWidth, comboBoxHeight);
	addAndMakeVisible(m_lfo1AssignDest);
	m_lfo1AssignDest.setBounds(283+comboBoxXMargin - comboBox3Width / 2, 700 - comboBoxHeight / 2, comboBox3Width, comboBoxHeight);

    addAndMakeVisible(m_lfo1Link);
	m_lfo1Link.setBounds(177 - m_lfo1Link.kWidth / 2, 416 - m_lfo1Link.kHeight / 2, m_lfo1Link.kWidth, m_lfo1Link.kHeight);  
    addAndMakeVisible(m_lfo1LfoMode);
	m_lfo1LfoMode.setBounds(102 - m_lfo1LfoMode.kWidth / 2, 879 - m_lfo1LfoMode.kHeight / 2, m_lfo1LfoMode.kWidth, m_lfo1LfoMode.kHeight);  
    addAndMakeVisible(m_lfo1EnvMode);
	m_lfo1EnvMode.setBounds(442 - m_lfo1EnvMode.kWidth / 2, 701 - m_lfo1EnvMode.kHeight / 2, m_lfo1EnvMode.kWidth, m_lfo1EnvMode.kHeight);  

	_parameterBinding.bind(m_lfo1Rate, Virus::Param_Lfo1Rate);
	_parameterBinding.bind(m_lfo1Symmetry, Virus::Param_Lfo1Symmetry);
	_parameterBinding.bind(m_lfo1Osc1Pitch, Virus::Param_Osc1Lfo1Amount);
	_parameterBinding.bind(m_lfo1Osc2Pitch, Virus::Param_Osc2Lfo1Amount);
	_parameterBinding.bind(m_lfo1Pw12, Virus::Param_PWLfo1Amount);
	_parameterBinding.bind(m_lfo1Reso12, Virus::Param_ResoLfo1Amount);
	_parameterBinding.bind(m_lfo1KeyFollow, Virus::Param_Lfo1Keyfollow);
	_parameterBinding.bind(m_lfo1Keytrigger, Virus::Param_Lfo1KeyTrigger);
	_parameterBinding.bind(m_lfo1AssignAmount, Virus::Param_Lfo1AssignAmount);
	_parameterBinding.bind(m_lfo1FilterGain, Virus::Param_FltGainLfo1Amount);

	_parameterBinding.bind(m_lfo1Clock, Virus::Param_Lfo1Clock);
	_parameterBinding.bind(m_lfo1Shape, Virus::Param_Lfo1Shape);
	_parameterBinding.bind(m_lfo1AssignDest, Virus::Param_Lfo1AssignDest);

	//todo no link! _parameterBinding.bind(m_lfo1Link, Virus::lfo);
	_parameterBinding.bind(m_lfo1LfoMode, Virus::Param_Lfo1Mode);
	_parameterBinding.bind(m_lfo1EnvMode, Virus::Param_Lfo1EnvMode);

    // LFO 2
	for (auto *s : {&m_lfo2Rate, &m_lfo2Symmetry, &m_lfo2Filter1Cutoff, &m_lfo2Filter2Cutoff, &m_lfo2Shape12,
					&m_lfo2Panorama, &m_lfo2KeyFollow, &m_lfo2Keytrigger, &m_lfo2AssignAmount, &m_lfo2AmtFM})
	{
		setupRotary(*this, *s);
	}

	m_lfo2Rate.setBounds(102 + 532 - knobSize / 2, 125 - knobSize / 2, knobSize, knobSize);
	m_lfo2Symmetry.setBounds(417 + 532 - knobSize / 2, 125 - knobSize / 2, knobSize, knobSize);
	m_lfo2Filter1Cutoff.setBounds(102 + 532 - knobSize / 2, 313 - knobSize / 2, knobSize, knobSize);
	m_lfo2Filter2Cutoff.setBounds(262 + 532 - knobSize / 2, 313 - knobSize / 2, knobSize, knobSize);
	m_lfo2Shape12.setBounds(417 + 532 - knobSize / 2, 313 - knobSize / 2, knobSize, knobSize);
	m_lfo2Panorama.setBounds(102 + 532 - knobSize / 2, 517 - knobSize / 2, knobSize, knobSize);
	m_lfo2KeyFollow.setBounds(258 + 532 - knobSize / 2, 517 - knobSize / 2, knobSize, knobSize);
	m_lfo2Keytrigger.setBounds(417 + 532 - knobSize / 2, 517 - knobSize / 2, knobSize, knobSize);
	m_lfo2AssignAmount.setBounds(102 + 532 - knobSize / 2, 705 - knobSize / 2, knobSize, knobSize);
	m_lfo2AmtFM.setBounds(417 + 532 - knobSize / 2, 886 - knobSize / 2, knobSize, knobSize);

	addAndMakeVisible(m_lfo2Clock);
	m_lfo2Clock.setBounds(258 + 532 + comboBoxXMargin - comboBoxWidth / 2, 84 - comboBoxHeight / 2, comboBoxWidth, comboBoxHeight);
	addAndMakeVisible(m_lfo2Shape);
	m_lfo2Shape.setBounds(258 + 532 +comboBoxXMargin - comboBoxWidth / 2, 169 - comboBoxHeight / 2, comboBoxWidth, comboBoxHeight);
	addAndMakeVisible(m_lfo2AssignDest);
	m_lfo2AssignDest.setBounds(283 + 532 +comboBoxXMargin - comboBox3Width / 2, 700 - comboBox3Height / 2, comboBox3Width, comboBox3Height);

	addAndMakeVisible(m_lfo2Link);
	m_lfo2Link.setBounds(177 + 532 - m_lfo2Link.kWidth / 2, 416 - m_lfo2Link.kHeight / 2, m_lfo2Link.kWidth, m_lfo2Link.kHeight);
	addAndMakeVisible(m_lfo2LfoMode);
	m_lfo2LfoMode.setBounds(102 + 532 - m_lfo2LfoMode.kWidth / 2, 879 - m_lfo2LfoMode.kHeight / 2, m_lfo2LfoMode.kWidth, m_lfo2LfoMode.kHeight);
	addAndMakeVisible(m_lfo2EnvMode);
	m_lfo2EnvMode.setBounds(442 + 532 - m_lfo2EnvMode.kWidth / 2, 701 - m_lfo2EnvMode.kHeight / 2, m_lfo2EnvMode.kWidth, m_lfo2EnvMode.kHeight);

	_parameterBinding.bind(m_lfo2Rate, Virus::Param_Lfo2Rate);
	_parameterBinding.bind(m_lfo2Symmetry, Virus::Param_Lfo2Symmetry);
	_parameterBinding.bind(m_lfo2Filter1Cutoff, Virus::Param_Cutoff1Lfo2Amount);
	_parameterBinding.bind(m_lfo2Filter2Cutoff, Virus::Param_Cutoff2Lfo2Amount);
	_parameterBinding.bind(m_lfo2Shape12, Virus::Param_OscShapeLfo2Amount);
	_parameterBinding.bind(m_lfo2Panorama, Virus::Param_PanoramaLfo2Amount);
	_parameterBinding.bind(m_lfo2KeyFollow, Virus::Param_Lfo2Keyfollow);
	_parameterBinding.bind(m_lfo2Keytrigger, Virus::Param_Lfo2Keytrigger);
	_parameterBinding.bind(m_lfo2AssignAmount, Virus::Param_Lfo2AssignAmount);
	_parameterBinding.bind(m_lfo2AmtFM, Virus::Param_FmAmountLfo2Amount);

	_parameterBinding.bind(m_lfo2Clock, Virus::Param_Lfo2Clock);
	_parameterBinding.bind(m_lfo2Shape, Virus::Param_Lfo2Shape);
	_parameterBinding.bind(m_lfo2AssignDest, Virus::Param_Lfo2AssignDest);

	// todo no link! _parameterBinding.bind(m_lfo2Link, Virus::lfo);
	_parameterBinding.bind(m_lfo2LfoMode, Virus::Param_Lfo2Mode);
	_parameterBinding.bind(m_lfo2EnvMode, Virus::Param_Lfo2EnvMode);


    // LFO 3
	for (auto *s : {&m_lfo3Rate, &m_lfo3FadeIn, &m_lfo3KeyFollow, &m_lfo3AssignAmount,})
	{
		setupRotary(*this, *s);
	}

	m_lfo3Rate.setBounds(1166 - knobSize / 2, 125 - knobSize / 2, knobSize, knobSize);
	m_lfo3FadeIn.setBounds(1166 - knobSize / 2, 313 - knobSize / 2, knobSize, knobSize);
	m_lfo3KeyFollow.setBounds(1322 - knobSize / 2, 313 - knobSize / 2, knobSize, knobSize);
	m_lfo3AssignAmount.setBounds(1166 - knobSize / 2, 517 - knobSize / 2, knobSize, knobSize);

	addAndMakeVisible(m_lfo3Clock);
	m_lfo3Clock.setBounds(1322+comboBoxXMargin - comboBoxWidth / 2, 84 - comboBoxHeight / 2, comboBoxWidth, comboBoxHeight);
	addAndMakeVisible(m_lfo3Shape);
	m_lfo3Shape.setBounds(1322+comboBoxXMargin - comboBoxWidth / 2, 169 - comboBoxHeight / 2, comboBoxWidth, comboBoxHeight);
	addAndMakeVisible(m_lfo3AssignDest);
	m_lfo3AssignDest.setBounds(1211+comboBoxXMargin - comboBox3Width / 2, 675 - comboBox3Height / 2, comboBox3Width, comboBox3Height);

	addAndMakeVisible(m_lfo3LfoMode);
	m_lfo3LfoMode.setBounds(1305 - m_lfo3LfoMode.kWidth / 2, 509 - m_lfo3LfoMode.kHeight / 2, m_lfo3LfoMode.kWidth, m_lfo3LfoMode.kHeight);

	_parameterBinding.bind(m_lfo3Rate, Virus::Param_Lfo3Rate);
	_parameterBinding.bind(m_lfo3FadeIn, Virus::Param_Lfo3FadeInTime);
	_parameterBinding.bind(m_lfo3KeyFollow, Virus::Param_Lfo3Keyfollow);
	_parameterBinding.bind(m_lfo3AssignAmount, Virus::Param_OscLfo3Amount);

	_parameterBinding.bind(m_lfo3Clock, Virus::Param_Lfo3Clock);
	_parameterBinding.bind(m_lfo3Shape, Virus::Param_Lfo3Shape);
	_parameterBinding.bind(m_lfo3AssignDest, Virus::Param_Lfo3Destination);

	// todo no link! _parameterBinding.bind(m_lfo3Link, Virus::lfo);
	_parameterBinding.bind(m_lfo3LfoMode, Virus::Param_Lfo3Mode);

    //Matrix Slo1
	for (auto *s : {&m_MatSlot1Amount1,&m_MatSlot1Amount2,&m_MatSlot1Amount3,&m_MatSlot1Amount4,
		 })
	{
		setupRotary(*this, *s);
	}

	m_MatSlot1Amount1.setBounds(1792 - knobSize / 2, 125 - knobSize / 2, knobSize, knobSize);
	m_MatSlot1Amount2.setBounds(2197 - knobSize / 2, 125 - knobSize / 2, knobSize, knobSize);
	m_MatSlot1Amount3.setBounds(1792 - knobSize / 2, 355 - knobSize / 2, knobSize, knobSize);
	m_MatSlot1Amount4.setBounds(2197 - knobSize / 2, 355 - knobSize / 2, knobSize, knobSize);

	addAndMakeVisible(m_MatSlot1Source1);
	m_MatSlot1Source1.setBounds(1597+comboBoxXMargin - comboBox3Width / 2, 82 - comboBox3Height / 2, comboBox3Width, comboBox3Height);
	addAndMakeVisible(m_MatSlot1Source2);
	m_MatSlot1Source2.setBounds(2002+comboBoxXMargin - comboBox3Width / 2, 82 - comboBox3Height / 2, comboBox3Width, comboBox3Height);
    addAndMakeVisible(m_MatSlot1Source3);
	m_MatSlot1Source3.setBounds(1597+comboBoxXMargin - comboBox3Width / 2, 313 - comboBox3Height / 2, comboBox3Width, comboBox3Height);
	addAndMakeVisible(m_MatSlot1Source4);
	m_MatSlot1Source4.setBounds(2002+comboBoxXMargin - comboBox3Width / 2, 313 - comboBox3Height / 2, comboBox3Width, comboBox3Height);
	addAndMakeVisible(m_MatSlot1AssignDest1);
	m_MatSlot1AssignDest1.setBounds(1597+comboBoxXMargin - comboBox3Width / 2, 171 - comboBox3Height / 2, comboBox3Width, comboBox3Height);
	addAndMakeVisible(m_MatSlot1AssignDest2);
	m_MatSlot1AssignDest2.setBounds(2002+comboBoxXMargin - comboBox3Width / 2, 171 - comboBox3Height / 2, comboBox3Width, comboBox3Height);
	addAndMakeVisible(m_MatSlot1AssignDest3);
	m_MatSlot1AssignDest3.setBounds(1597+comboBoxXMargin- comboBox3Width / 2, 400 - comboBox3Height / 2, comboBox3Width, comboBox3Height);
	addAndMakeVisible(m_MatSlot1AssignDest4);
	m_MatSlot1AssignDest4.setBounds(2002+comboBoxXMargin - comboBox3Width / 2, 400 - comboBox3Height / 2, comboBox3Width, comboBox3Height);

	_parameterBinding.bind(m_MatSlot1Source1, Virus::Param_Assign1Source);
	_parameterBinding.bind(m_MatSlot1Source2, Virus::Param_Assign4Source);
	_parameterBinding.bind(m_MatSlot1Source3, Virus::Param_Assign5Source);
	_parameterBinding.bind(m_MatSlot1Source4, Virus::Param_Assign6Source);
	_parameterBinding.bind(m_MatSlot1Amount1, Virus::Param_Assign1Amount);
	_parameterBinding.bind(m_MatSlot1Amount2, Virus::Param_Assign4Amount);
	_parameterBinding.bind(m_MatSlot1Amount3, Virus::Param_Assign5Amount);
	_parameterBinding.bind(m_MatSlot1Amount4, Virus::Param_Assign6Amount);
	_parameterBinding.bind(m_MatSlot1AssignDest1, Virus::Param_Assign1Destination);
	_parameterBinding.bind(m_MatSlot1AssignDest2, Virus::Param_Assign4Destination);
	_parameterBinding.bind(m_MatSlot1AssignDest3, Virus::Param_Assign5Destination);
	_parameterBinding.bind(m_MatSlot1AssignDest4, Virus::Param_Assign6Destination);

    // Matrix Slot 2
	for (auto *s : { &m_MatSlot2Amount1, &m_MatSlot2Amount2})
	{
		setupRotary(*this, *s);
	}

	m_MatSlot2Amount1.setBounds(1792 - knobSize / 2, 624 - knobSize / 2, knobSize, knobSize);
	m_MatSlot2Amount2.setBounds(2197 - knobSize / 2, 624 - knobSize / 2, knobSize, knobSize);
	addAndMakeVisible(m_MatSlot2Source12);
	m_MatSlot2Source12.setBounds(1597+comboBoxXMargin - comboBox3Width / 2, 576 - comboBox3Height / 2, comboBox3Width, comboBox3Height);
	addAndMakeVisible(m_MatSlot2AssignDest1);
	m_MatSlot2AssignDest1.setBounds(1597+comboBoxXMargin - comboBox3Width / 2, 666 - comboBox3Height / 2, comboBox3Width, comboBox3Height);
	addAndMakeVisible(m_MatSlot2AssignDest2);
	m_MatSlot2AssignDest2.setBounds(2002+comboBoxXMargin - comboBox3Width / 2, 624 - comboBox3Height / 2, comboBox3Width, comboBox3Height);

	_parameterBinding.bind(m_MatSlot2Source12, Virus::Param_Assign2Source);
	_parameterBinding.bind(m_MatSlot2Amount1, Virus::Param_Assign2Amount1);
	_parameterBinding.bind(m_MatSlot2Amount2, Virus::Param_Assign2Amount2);
	_parameterBinding.bind(m_MatSlot2AssignDest1, Virus::Param_Assign2Destination1);
	_parameterBinding.bind(m_MatSlot2AssignDest2, Virus::Param_Assign2Destination2);


    // Matrix Slot 3
	for (auto *s : {&m_MatSlot3Amount1,&m_MatSlot3Amount2,&m_MatSlot3Amount3})
	{
		setupRotary(*this, *s);
	}

	m_MatSlot3Amount1.setBounds(1404 - knobSize / 2, 886 - knobSize / 2, knobSize, knobSize);
	m_MatSlot3Amount2.setBounds(1792 - knobSize / 2, 886 - knobSize / 2, knobSize, knobSize);
	m_MatSlot3Amount3.setBounds(2197 - knobSize / 2, 886 - knobSize / 2, knobSize, knobSize);

	addAndMakeVisible(m_MatSlot3Source123);
	m_MatSlot3Source123.setBounds(1210+comboBoxXMargin - comboBox3Width / 2, 838 - comboBox3Height / 2, comboBox3Width, comboBox3Height);

	addAndMakeVisible(m_MatSlot3AssignDest1);
	m_MatSlot3AssignDest1.setBounds(1210+comboBoxXMargin - comboBox3Width / 2, 928 - comboBox3Height / 2, comboBox3Width, comboBox3Height);

	addAndMakeVisible(m_MatSlot3AssignDest2);
	m_MatSlot3AssignDest2.setBounds(1596+comboBoxXMargin - comboBox3Width / 2, 888 - comboBox3Height / 2, comboBox3Width, comboBox3Height);

	addAndMakeVisible(m_MatSlot3AssignDest3);
	m_MatSlot3AssignDest3.setBounds(2002+comboBoxXMargin - comboBox3Width / 2, 888 - comboBox3Height / 2, comboBox3Width, comboBox3Height);

	_parameterBinding.bind(m_MatSlot3Source123, Virus::Param_Assign3Source);

	_parameterBinding.bind(m_MatSlot3Amount1, Virus::Param_Assign3Amount1);
	_parameterBinding.bind(m_MatSlot3Amount2, Virus::Param_Assign3Amount2);
	_parameterBinding.bind(m_MatSlot3Amount3, Virus::Param_Assign2Amount3); //todo: need fix typ --> Param_Assign3Amount3

	_parameterBinding.bind(m_MatSlot3AssignDest1, Virus::Param_Assign3Destination1);
	_parameterBinding.bind(m_MatSlot3AssignDest2, Virus::Param_Assign3Destination2);
	_parameterBinding.bind(m_MatSlot3AssignDest3, Virus::Param_Assign2Destination3); //todo: need fix typ --> Param_Assign3Destination3
}
