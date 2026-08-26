#include "parameterdescription.h"

namespace pluginLib
{
	bool Description::isNonPartSensitive() const
	{
		if(isSoftKnob())
			return false;
		return classFlags & static_cast<int>(pluginLib::ParameterClass::Global) || (classFlags & static_cast<int>(pluginLib::ParameterClass::NonPartSensitive));
	}

	bool Description::isSoftKnob() const
	{
		return !softKnobTargetSelect.empty() && !softKnobTargetList.empty();
	}
}
