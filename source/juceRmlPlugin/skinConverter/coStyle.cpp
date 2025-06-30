#include "coStyle.h"

#include <sstream>

#include "scHelper.h"
#include "juce_graphics/juce_graphics.h"

namespace rmlPlugin::skinConverter
{
	void CoStyle::add(const std::string& _key, const juce::Colour& _color)
	{
		if (_color.getAlpha() == 0)
			return;
		add(_key, helper::toRmlColorString(_color));
	}

	void CoStyle::write(std::stringstream& _out, const std::string& _name, uint32_t _depth) const
	{
		const auto prefix = std::string(_depth, '\t');

		_out << prefix << _name << '\n';
		_out << prefix << "{\n";

		for (const auto& [id, property] : properties)
			_out << prefix << "\t" << id << ": " << property << ";\n";

		_out << prefix << "}\n";
	}

	void CoStyle::writeInline(std::stringstream& _ss)
	{
		for (const auto& [key, value] : properties)
			_ss << key << ": " << value << ";";
	}
}
