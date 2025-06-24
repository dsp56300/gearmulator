#include "coStyle.h"

#include <sstream>

namespace rmlPlugin::skinConverter
{
	void CoStyle::write(std::stringstream& _out, const std::string& _name, uint32_t _depth) const
	{
		const auto prefix = std::string(_depth, '\t');

		_out << prefix << _name << '\n';
		_out << prefix << "{\n";
		for (const auto& [id, property] : properties)
		{
			_out << prefix << "\t" << id << ": " << property << '\n';
		}
		_out << prefix << "}\n";
	}
}
