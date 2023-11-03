#include "patchdbtypes.h"

namespace pluginLib::patchDB
{
	constexpr const char* const g_sourceTypes[] =
	{
		"invalid",
		"rom",
		"file",
		"folder"
	};

	static_assert(std::size(g_sourceTypes) == static_cast<uint32_t>(SourceType::Count));

	std::string toString(SourceType _type)
	{
		const auto i = static_cast<uint32_t>(_type);
		if (i >= std::size(g_sourceTypes))
			return {};
		return g_sourceTypes[i];
	}

	SourceType toSourceType(const std::string& _string)
	{
		size_t i = 0;
		for (const auto& sourceType : g_sourceTypes)
		{
			if(sourceType == _string)
				return static_cast<SourceType>(i);
			++i;
		}
		return SourceType::Invalid;
	}
}
