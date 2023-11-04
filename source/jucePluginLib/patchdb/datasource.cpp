#include "datasource.h"

#include <sstream>

namespace pluginLib::patchDB
{
	std::string DataSource::toString() const
	{
		std::stringstream ss;

		ss << "type|" << patchDB::toString(type);
		ss << "|name|" << name;
		if (bank != g_invalidBank)
			ss << "|bank|" << bank;
//		if (program != g_invalidProgram)
//			ss << "|prog|" << program;
		return ss.str();
	}
}
