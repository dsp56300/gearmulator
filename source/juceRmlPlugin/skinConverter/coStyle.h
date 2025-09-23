#pragma once

#include <string>
#include <sstream>
#include <map>

namespace juce
{
	class Colour;
}

namespace rmlPlugin::skinConverter
{
	struct CoStyle
	{
		std::map<std::string, std::string> properties;

		void add(std::string _key, std::string _value)
		{
			properties.insert({ std::move(_key), std::move(_value)});
		}

		void add(const std::string& _key, const juce::Colour& _color);

		void write(std::ostream& _out, const std::string& _name, uint32_t _depth) const;
		void writeInline(std::stringstream& _ss);

		bool operator == (const CoStyle& _other) const
		{
			return properties == _other.properties;
		}

		bool empty() const { return properties.empty(); }
	};

	struct CoSpritesheet : CoStyle
	{
		uint32_t spriteCount = 0;
	};
}
