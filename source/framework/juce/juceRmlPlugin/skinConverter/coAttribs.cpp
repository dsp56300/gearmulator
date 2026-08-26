#include "coAttribs.h"

#include <sstream>

#include "RmlUi/Core/CoreInstance.h"

namespace rmlPlugin::skinConverter
{
	namespace
	{
		Rml::CoreInstance g_coreInstance;
	}

	void CoAttribs::write(std::stringstream& _ss) const
	{
		if (attributes.empty())
			return;

		bool first = true;

		for (const auto& [key, value] : attributes)
		{
			std::string v;

			if (!value.GetInto(g_coreInstance, v))
				continue;

			if (v.empty())
				continue;

			if (first)
				first = false;
			else
				_ss << " ";

			_ss << key << "=\"" << v << "\"";
		}
	}
}
