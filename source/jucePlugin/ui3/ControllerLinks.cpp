#include "ControllerLinks.h"

#include "VirusEditor.h"

namespace genericVirusUI
{
	// The only purpose of this class is to provide backwards-compatibility with old skins that do not have their links expressed in the json description file
	ControllerLinks::ControllerLinks(const VirusEditor& _editor)
	{
		std::vector<juce::Slider*> lfoSliderL, lfoSliderR, envVel;
		std::vector<juce::Button*> lfoToggles;

		_editor.findComponents(lfoSliderL, "LfoOsc1Pitch");
		_editor.findComponents(lfoSliderR, "LfoOsc2Pitch");

		_editor.findComponents(lfoToggles, "Lfo1Link");
		if(lfoToggles.empty())
			_editor.findComponents(lfoToggles, "LfoOscPitchLink");

		_editor.findComponents(envVel, "EnvVel");
		if(envVel.empty())
			_editor.findComponents(envVel, "EnvVelo");

		auto* linkEnv = _editor.findComponentT<juce::Button>("LinkEnv", false);

		auto* flt1Vel = _editor.findComponentT<juce::Slider>("VelFlt1Freq", false);
		auto* flt2Vel = _editor.findComponentT<juce::Slider>("VelFlt2Freq", false);
		
		if(lfoSliderL.size() == 2 && lfoSliderR.size() == 2 && lfoToggles.size() == 2)
		{
			for(size_t i=0; i<2; ++i)
				createLink(lfoSliderL[i], lfoSliderR[i], lfoToggles[i]);
		}

		if(linkEnv)
		{
			if(envVel.size() == 2)
				createLink(envVel[0], envVel[1], linkEnv);

			if(flt1Vel && flt2Vel)
				createLink(flt1Vel, flt2Vel, linkEnv);
		}

		auto* vocModQ = _editor.findComponentT<juce::Slider>("VocModQ", false);
		auto* vocCarrSpread = _editor.findComponentT<juce::Slider>("VocCarrSpread", false);
		auto* vocLink = _editor.findComponentT<juce::Button>("VocoderLink", false);

		if(vocModQ && vocCarrSpread && vocLink)
			createLink(vocModQ, vocCarrSpread, vocLink);
	}

	void ControllerLinks::createLink(juce::Slider* _a, juce::Slider* _b, juce::Button* _cond)
	{
		auto* link = new genericUI::ControllerLink();
		link->create(_a, _b, _cond);
		m_links.emplace_back(link);

		link = new genericUI::ControllerLink();
		link->create(_b, _a, _cond);
		m_links.emplace_back(link);
	}
}
