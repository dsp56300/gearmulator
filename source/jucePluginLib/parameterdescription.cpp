#include "parameterdescription.h"

namespace pluginLib
{
	bool Description::isNonPartSensitive() const
	{
		return classFlags & static_cast<int>(pluginLib::ParameterClass::Global) || (classFlags & static_cast<int>(pluginLib::ParameterClass::NonPartSensitive));
	}
}
