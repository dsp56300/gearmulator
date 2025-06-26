#include "convertedObject.h"

#include <sstream>

namespace rmlPlugin::skinConverter
{
	void ConvertedObject::write(std::stringstream& _ss, const size_t _depth)
	{
		_ss << std::string(_depth, '\t') << '<' << tag;
		if (!id.empty())
			_ss << " id=\"" << id << "\"";
		if (!classes.empty())
		{
			_ss << " class=\"";
			bool first = true;
			for (const auto& cls : classes)
			{
				if (!first)
					_ss << " ";
				else
					first = false;
				_ss << cls;
			}
			_ss << "\"";
		}

		if (!attribs.attributes.empty())
			_ss << ' ';
		attribs.write(_ss);

		_ss << " style=\"";

		position.write(_ss);

		if (!style.properties.empty())
		{
			_ss << ' ';
			style.writeInline(_ss);
		}

		_ss << "\"";

		if (innerText.empty() && children.empty())
		{
			_ss << "/>\n";
			return;
		}
		_ss << ">";
		if (!innerText.empty())
		{
			_ss << innerText;
			_ss << "</" << tag << ">\n";
		}
		else
		{
			_ss << '\n';

			for (auto& child : children)
				child.write(_ss, _depth + 1);
			_ss << std::string(_depth, '\t') << "</" << tag << ">\n";
		}
	}

	void ConvertedObject::set(const std::string& _id, const genericUI::UiObject& _object)
	{
		id = _id;
		position.set(_object);
	}
}
