#include "jeAssign.h"

#include <unordered_map>

#include "jeController.h"
#include "jeEditor.h"

#include "jeLib/jemiditypes.h"
#include "juceRmlPlugin/rmlParameterBinding.h"

#include "juceRmlUi/rmlElemButton.h"

namespace jeJucePlugin
{
	const std::unordered_map<jeLib::Patch, jeLib::Patch> g_velocityMap
	{
	    {jeLib::Patch::Lfo1Rate, jeLib::Patch::VelocityLfo1Rate},
	    {jeLib::Patch::Lfo1Fade, jeLib::Patch::VelocityLfo1Fade},
	    {jeLib::Patch::Lfo2Rate, jeLib::Patch::VelocityLfo2Rate},
	    {jeLib::Patch::CrossModulationDepth, jeLib::Patch::VelocityCrossModulationDepth},
	    {jeLib::Patch::OscillatorBalance, jeLib::Patch::VelocityOscillatorBalance},
	    {jeLib::Patch::OscLfo1Depth, jeLib::Patch::VelocityPitchLfo1Depth},
	    {jeLib::Patch::PitchLfo2Depth, jeLib::Patch::VelocityPitchLfo2Depth},
	    {jeLib::Patch::PitchEnvelopeDepth, jeLib::Patch::VelocityPitchEnvelopeDepth},
	    {jeLib::Patch::PitchEnvelopeAttackTime, jeLib::Patch::VelocityPitchEnvelopeAttackTime},
	    {jeLib::Patch::PitchEnvelopeDecayTime, jeLib::Patch::VelocityPitchEnvelopeDecayTime},
	    {jeLib::Patch::Osc1Control1, jeLib::Patch::VelocityOsc1Control1},
	    {jeLib::Patch::Osc1Control2, jeLib::Patch::VelocityOsc1Control2},
	    {jeLib::Patch::Osc2Range, jeLib::Patch::VelocityOsc2Range},
	    {jeLib::Patch::Osc2FineWide, jeLib::Patch::VelocityOsc2FineWide},
	    {jeLib::Patch::Osc2Control1, jeLib::Patch::VelocityOsc2Control1},
	    {jeLib::Patch::Osc2Control2, jeLib::Patch::VelocityOsc2Control2},
	    {jeLib::Patch::CutoffFrequency, jeLib::Patch::VelocityCutoffFrequency},
	    {jeLib::Patch::Resonance, jeLib::Patch::VelocityResonance},
	    {jeLib::Patch::CutoffFrequencyKeyFollow, jeLib::Patch::VelocityCutoffFreqKeyFollow},
	    {jeLib::Patch::FilterLfo1Depth, jeLib::Patch::VelocityFilterLfo1Depth},
	    {jeLib::Patch::FilterLfo2Depth, jeLib::Patch::VelocityFilterLfo2Depth},
	    {jeLib::Patch::FilterEnvelopeDepth, jeLib::Patch::VelocityFilterEnvDepth},
	    {jeLib::Patch::FilterEnvelopeAttackTime, jeLib::Patch::VelocityFilterEnvAttackTime},
	    {jeLib::Patch::FilterEnvelopeDecayTime, jeLib::Patch::VelocityFilterEnvDecayTime},
	    {jeLib::Patch::FilterEnvelopeSustainLevel, jeLib::Patch::VelocityFilterEnvSusLevel},
	    {jeLib::Patch::FilterEnvelopeReleaseTime, jeLib::Patch::VelocityFilterEnvReleaseTime},
	    {jeLib::Patch::AmpLevel, jeLib::Patch::VelocityAmpLevel},
	    {jeLib::Patch::AmpLfo1Depth, jeLib::Patch::VelocityAmpLfo1Depth},
	    {jeLib::Patch::AmpLfo2Depth, jeLib::Patch::VelocityAmpLfo2Depth},
	    {jeLib::Patch::AmpEnvelopeAttackTime, jeLib::Patch::VelocityAmpEnvAttackTime},
	    {jeLib::Patch::AmpEnvelopeDecayTime, jeLib::Patch::VelocityAmpEnvDecayTime},
	    {jeLib::Patch::AmpEnvelopeSustainLevel, jeLib::Patch::VelocityAmpEnvSustainLevel},
	    {jeLib::Patch::AmpEnvelopeReleaseTime, jeLib::Patch::VelocityAmpEnvReleaseTime},
	    {jeLib::Patch::ToneControlBass, jeLib::Patch::VelocityToneControlBass},
	    {jeLib::Patch::ToneControlTreble, jeLib::Patch::VelocityToneControlTreble},
	    {jeLib::Patch::MultiEffectsLevel, jeLib::Patch::VelocityMultiEffectsLevel},
	    {jeLib::Patch::DelayTime, jeLib::Patch::VelocityDelayTime},
	    {jeLib::Patch::DelayFeedback, jeLib::Patch::VelocityDelayFeedback},
	    {jeLib::Patch::DelayLevel, jeLib::Patch::VelocityDelayLevel},
		{jeLib::Patch::PortamentoTime , jeLib::Patch::VelocityPortamentoTime},
	};

	const std::unordered_map<jeLib::Patch, jeLib::Patch> g_controlMap
	{
	    {jeLib::Patch::Lfo1Rate, jeLib::Patch::ControlLfo1Rate},
	    {jeLib::Patch::Lfo1Fade, jeLib::Patch::ControlLfo1Fade},
	    {jeLib::Patch::Lfo2Rate, jeLib::Patch::ControlLfo2Rate},
	    {jeLib::Patch::CrossModulationDepth, jeLib::Patch::ControlCrossModulationDepth},
	    {jeLib::Patch::OscillatorBalance, jeLib::Patch::ControlOscillatorBalance},
	    {jeLib::Patch::OscLfo1Depth, jeLib::Patch::ControlPitchLfo1Depth},
	    {jeLib::Patch::PitchLfo2Depth, jeLib::Patch::ControlPitchLfo2Depth},
	    {jeLib::Patch::PitchEnvelopeDepth, jeLib::Patch::ControlPitchEnvelopeDepth},
	    {jeLib::Patch::PitchEnvelopeAttackTime, jeLib::Patch::ControlPitchEnvelopeAttackTime},
	    {jeLib::Patch::PitchEnvelopeDecayTime, jeLib::Patch::ControlPitchEnvelopeDecayTime},
	    {jeLib::Patch::Osc1Control1, jeLib::Patch::ControlOsc1Control1},
	    {jeLib::Patch::Osc1Control2, jeLib::Patch::ControlOsc1Control2},
	    {jeLib::Patch::Osc2Range, jeLib::Patch::ControlOsc2Range},
	    {jeLib::Patch::Osc2FineWide, jeLib::Patch::ControlOsc2FineWide},
	    {jeLib::Patch::Osc2Control1, jeLib::Patch::ControlOsc2Control1},
	    {jeLib::Patch::Osc2Control2, jeLib::Patch::ControlOsc2Control2},
	    {jeLib::Patch::CutoffFrequency, jeLib::Patch::ControlCutoffFrequency},
	    {jeLib::Patch::Resonance, jeLib::Patch::ControlResonance},
	    {jeLib::Patch::CutoffFrequencyKeyFollow, jeLib::Patch::ControlCutoffFreqKeyFollow},
	    {jeLib::Patch::FilterLfo1Depth, jeLib::Patch::ControlFilterLfo1Depth},
	    {jeLib::Patch::FilterLfo2Depth, jeLib::Patch::ControlFilterLfo2Depth},
	    {jeLib::Patch::FilterEnvelopeDepth, jeLib::Patch::ControlFilterEnvDepth},
	    {jeLib::Patch::FilterEnvelopeAttackTime, jeLib::Patch::ControlFilterEnvAttackTime},
	    {jeLib::Patch::FilterEnvelopeDecayTime, jeLib::Patch::ControlFilterEnvDecayTime},
	    {jeLib::Patch::FilterEnvelopeSustainLevel, jeLib::Patch::ControlFilterEnvSustainLevel},
	    {jeLib::Patch::FilterEnvelopeReleaseTime, jeLib::Patch::ControlFilterEnvReleaseTime},
	    {jeLib::Patch::AmpLevel, jeLib::Patch::ControlAmpLevel},
	    {jeLib::Patch::AmpLfo1Depth, jeLib::Patch::ControlAmpLfo1Depth},
	    {jeLib::Patch::AmpLfo2Depth, jeLib::Patch::ControlAmpLfo2Depth},
	    {jeLib::Patch::AmpEnvelopeAttackTime, jeLib::Patch::ControlAmpEnvAttackTime},
	    {jeLib::Patch::AmpEnvelopeDecayTime, jeLib::Patch::ControlAmpEnvDecayTime},
	    {jeLib::Patch::AmpEnvelopeSustainLevel, jeLib::Patch::ControlAmpEnvSustainLevel},
	    {jeLib::Patch::AmpEnvelopeReleaseTime, jeLib::Patch::ControlAmpEnvReleaseTime},
	    {jeLib::Patch::ToneControlBass, jeLib::Patch::ControlToneControlBass},
	    {jeLib::Patch::ToneControlTreble, jeLib::Patch::ControlToneControlTreble},
	    {jeLib::Patch::MultiEffectsLevel, jeLib::Patch::ControlMultiEffectsLevel},
	    {jeLib::Patch::DelayTime, jeLib::Patch::ControlDelayTime},
	    {jeLib::Patch::DelayFeedback, jeLib::Patch::ControlDelayFeedback},
	    {jeLib::Patch::DelayLevel, jeLib::Patch::ControlDelayLevel},
		{jeLib::Patch::PortamentoTime , jeLib::Patch::ControlPortamentoTime},
	};

	JeAssign::JeAssign(const Editor& _editor) : m_editor(_editor)
	{
		m_btAssignVelocity = _editor.findChild<juceRmlUi::ElemButton>("ActiveIndicatorOfVelocityAssign");
		m_btAssignControl = _editor.findChild<juceRmlUi::ElemButton>("ActiveIndicatorOfControlAssign");

		const auto& binding = m_editor.getRmlParameterBinding();

		for (auto [type, paramVelocity] : g_velocityMap)
		{
			const auto params = getParameter(type);

			assert(!params.empty());

			for (auto* param : params)
			{
				std::vector<Rml::Element*> elements;
				binding->getElementsForParameter(elements, param, false);
				assert(!elements.empty());

				for (auto* elem : elements)
					m_controls[type].emplace_back(elem, param->getDescription().name);
			}
		}

		juceRmlUi::EventListener::Add(m_btAssignVelocity, Rml::EventId::Click, [this](Rml::Event& _event)
		{
			onClick(_event, m_btAssignVelocity, AssignType::Velocity);
		});
		juceRmlUi::EventListener::Add(m_btAssignControl, Rml::EventId::Click, [this](Rml::Event& _event)
		{
			onClick(_event, m_btAssignControl, AssignType::Control);
		});
	}

	void JeAssign::setAssignType(AssignType _type)
	{
		if (m_assignType == _type)
			return;

		m_assignType = _type;

		m_btAssignVelocity->setChecked(m_assignType == AssignType::Velocity);
		m_btAssignControl->setChecked(m_assignType == AssignType::Control);

		const auto& binding = m_editor.getRmlParameterBinding();

		for (const auto& [type, controls] : m_controls)
		{
			auto t = type;
			switch (m_assignType)
			{
			case AssignType::Velocity:
				{
					auto it = g_velocityMap.find(t);
					if (it != g_velocityMap.end())
						t = it->second;
				}
				break;
			case AssignType::Control:
				{
					auto it = g_controlMap.find(t);
					if (it != g_controlMap.end())
						t = it->second;
				}
				break;
			default:;
			}

			auto parameters = getParameter(t);

			if (m_assignType != AssignType::None)
			{
				assert(parameters.size() == 1);
			}

			auto& c = m_editor.getJeController();

			for (const auto& it : controls)
			{
				auto* control = it.first;
				pluginLib::Parameter* parameter;

				if (m_assignType != AssignType::None)
					parameter = parameters.front();
				else
					parameter = c.getParameter(it.second, c.getCurrentPart());

				assert(parameter);

				control->SetPseudoClass("assignvelocity", m_assignType == AssignType::Velocity);
				control->SetPseudoClass("assigncontrol", m_assignType == AssignType::Control);

				binding->bind(*control, parameter->getDescription().name);
			}
		}
	}

	std::vector<pluginLib::Parameter*> JeAssign::getParameter(jeLib::Patch _type) const
	{
		auto& c = m_editor.getJeController();

		const auto p = static_cast<uint32_t>(_type);
		const uint8_t page = static_cast<uint8_t>(p >> 8);
		const uint8_t index = p & 0x7f;

		std::vector<pluginLib::Parameter*> parameters = c.findSynthParam(c.getCurrentPart(), page, index);
		assert(!parameters.empty());
		return parameters;
	}

	void JeAssign::onClick(Rml::Event& _event, const juceRmlUi::ElemButton* _button, const AssignType _type)
	{
		if (m_assignType == _type)
			setAssignType(AssignType::None);
		else
			setAssignType(_type);
	}
}
