#pragma once

#include <map>

#include "RmlUi/Core/Property.h"

namespace rmlPlugin::skinConverter
{
	struct CoStyle
	{
		std::map<std::string, std::string> properties;

		void add(std::string _key, std::string _value)
		{
			properties.insert({ std::move(_key), std::move(_value)});
		}

		void write(std::stringstream& _out, const std::string& _name, uint32_t _depth) const;
		void writeInline(std::stringstream& _ss);

		bool operator == (const CoStyle& _other) const
		{
			return properties == _other.properties;
		}
	};
}
