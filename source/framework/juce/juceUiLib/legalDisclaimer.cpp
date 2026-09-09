#include "legalDisclaimer.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include <utility>

namespace genericUI
{
	void showLegalDisclaimer(const std::string& _productName, std::function<void()> _accepted)
	{
		const auto options = juce::MessageBoxOptions::makeOptionsOk(juce::MessageBoxIconType::WarningIcon,
			_productName,
			"It is the sole responsibility of the user to operate this emulator within the bounds of all applicable laws.\n\n"
			"Usage of emulators in conjunction with ROM images you are not legally entitled to own is forbidden by copyright law.\n\n"
			"If you are not legally entitled to use this emulator please discontinue usage immediately.\n\n",
			"I Agree");
		juce::NativeMessageBox::showAsync(options, [accepted = std::move(_accepted)](int)
		{
			if(accepted)
				accepted();
		});
	}
}
