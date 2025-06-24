#pragma once

#include <map>
#include <string>

#include "RmlUi/Core/Variant.h"

namespace rmlPlugin::skinConverter
{
	struct CoAttribs
	{
		std::map<std::string, Rml::Variant> attributes;

		void write(std::stringstream& _ss) const;

		void set(const char* _key, const std::string& _value)
		{
			attributes.insert({_key, Rml::Variant(_value)});
		}
	};
}
