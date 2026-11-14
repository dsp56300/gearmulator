#pragma once

#include <string>

#include "RmlUi/Core/StringUtilities.h"

namespace pluginManager
{
	// What goes inside a chip that can be ticked: the box that says whether it is, then its name
	// (BUG-10562). The rows have the same in their templates; this is for the chips built in C++.
	inline std::string tickChipRml(const std::string& _name)
	{
		return R"(<span class="tick-box"><svg class="tick" src="icon_check.svg"/></span>)" +
			Rml::StringUtilities::EncodeRml(_name);
	}
}
