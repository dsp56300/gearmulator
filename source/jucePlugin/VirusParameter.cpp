#include "VirusParameter.h"

namespace Virus
{
	Parameter::Parameter(pluginLib::Controller& _controller, const pluginLib::Description& _desc, uint8_t _partNum,	int _uniqueId)
		: pluginLib::Parameter(_controller, _desc, _partNum, _uniqueId)
	{
	}

	float Parameter::getDefaultValue() const
	{
		const auto& desc = getDescription();

		// default value should probably be in description instead of hardcoded like this.
		if (desc.index == 21 || desc.index == 31) // osc keyfollows
			return (64.0f + 32.0f) / 127.0f;
		if (desc.index == 36) // osc vol / saturation
			return 0.5f;
		if (desc.index == 40) // filter1 cutoffs
			return 1.0f;
		if(desc.index == 41) //filter 2
			return 0.5f;
		if(desc.index == 91) // patch volume
			return 100.0f / 127.0f;
		return desc.isBipolar ? 0.5f : 0.0f; /* maybe return from ROM state? */
	}
}
