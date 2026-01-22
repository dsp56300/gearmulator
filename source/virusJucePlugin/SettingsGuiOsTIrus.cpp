#include "SettingsGuiOsTIrus.h"

#include "VirusEditor.h"

#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

namespace genericVirusUI
{
	SettingsGuiOsTIrus::SettingsGuiOsTIrus(const VirusEditor* _editor, Rml::Element* _root)
	{
		auto* leds = _editor->getLeds().get();

		if(!leds || !leds->supportsLogoAnimation())
		{
			juceRmlUi::helper::setVisible(_root, false);
			return;
		}

		auto* button = juceRmlUi::helper::findChild(_root, "btEnableLogoAnimations");

		auto* buttonComp = juceRmlUi::helper::findChildT<juceRmlUi::ElemButton>(button, "button");

		buttonComp->setChecked(leds->isLogoAnimationEnabled());

		juceRmlUi::EventListener::AddClick(button, [leds, buttonComp]
		{
			leds->toggleLogoAnimation();

			buttonComp->setChecked(leds->isLogoAnimationEnabled());
		});
	}
}
