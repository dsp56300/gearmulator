#include "ControllerLinks.h"

#include "VirusEditor.h"
#include "juceRmlPlugin/rmlPluginDocument.h"
#include "juceRmlUi/rmlElemButton.h"

namespace genericVirusUI
{
	// The only purpose of this class is to provide backwards-compatibility with old skins that do not have their links expressed in the json description file
	void ControllerLinks::create(const VirusEditor& _editor)
	{
		auto createLink = [&_editor](Rml::Element* _a, Rml::Element* _b, Rml::Element* _cond)
		{
			_editor.getRmlPluginDocument()->addControllerLink(_a, _b, _cond);
		};

		std::vector<Rml::Element*> lfoSliderL, lfoSliderR, envVel;
		std::vector<Rml::Element*> lfoToggles;

		auto* r = _editor.getRmlRootElement();

		juceRmlUi::helper::findChildren(lfoSliderL, r, "LfoOsc1Pitch");
		juceRmlUi::helper::findChildren(lfoSliderL, r, "LfoOsc1Pitch");
		juceRmlUi::helper::findChildren(lfoSliderR, r, "LfoOsc2Pitch");

		juceRmlUi::helper::findChildren(lfoToggles, r, "Lfo1Link");
		if(lfoToggles.empty())
			juceRmlUi::helper::findChildren(lfoToggles, r, "LfoOscPitchLink");

		juceRmlUi::helper::findChildren(envVel, r, "EnvVel");
		if(envVel.empty())
			juceRmlUi::helper::findChildren(envVel, r, "EnvVelo");

		auto* linkEnv = _editor.findChild<juceRmlUi::ElemButton>("LinkEnv", false);

		auto* flt1Vel = _editor.findChild("VelFlt1Freq", false);
		auto* flt2Vel = _editor.findChild("VelFlt2Freq", false);

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

		auto* vocModQ = _editor.findChild("VocModQ", false);
		auto* vocCarrSpread = _editor.findChild("VocCarrSpread", false);
		auto* vocLink = _editor.findChild("VocoderLink", false);

		if(vocModQ && vocCarrSpread && vocLink)
			createLink(vocModQ, vocCarrSpread, vocLink);
	}
}
