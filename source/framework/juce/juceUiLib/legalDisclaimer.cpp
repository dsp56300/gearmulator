#include "legalDisclaimer.h"

#include "messageBox.h"

#include <utility>

namespace genericUI
{
	void showLegalDisclaimer(const std::string& _productName, std::function<void()> _accepted)
	{
		MessageBox::showNotice(MessageBox::Icon::Warning,
			_productName,
			"It is the sole responsibility of the user to operate this emulator within the bounds of all applicable laws.\n\n"
			"Usage of emulators in conjunction with ROM images you are not legally entitled to own is forbidden by copyright law.\n\n"
			"If you are not legally entitled to use this emulator please discontinue usage immediately.\n\n",
			"I Agree", std::move(_accepted));
	}
}
