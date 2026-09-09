#pragma once

#include <functional>
#include <string>

namespace genericUI
{
	// Shared legal gate for every firmware-backed editor. Persistence remains
	// owned by the caller so plugin and desktop shells can use their own config.
	void showLegalDisclaimer(const std::string& _productName, std::function<void()> _accepted);
}
