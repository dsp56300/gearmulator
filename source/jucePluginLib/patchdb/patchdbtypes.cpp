#include "patchdbtypes.h"

namespace pluginLib::patchDB
{
	constexpr std::initializer_list<const char*> g_sourceTypes =
	{
		"invalid",
		"rom",
		"folder",
		"file"
	};

	static_assert(std::size(g_sourceTypes) == static_cast<uint32_t>(SourceType::Count));

	constexpr std::initializer_list<const char*> g_tagTypes =
	{
		"invalid",
		"category",
		"tag",
		"favourites",
		"customA",
		"customB",
		"customC",
	};

	static_assert(std::size(g_tagTypes) == static_cast<uint32_t>(TagType::Count));

	template<typename Tenum>
	const char* toString(Tenum _type, const std::initializer_list<const char*>& _strings)
	{
		const auto i = static_cast<uint32_t>(_type);
		if (i >= _strings.size())
			return "";
		return *(_strings.begin() + i);
	}

	std::string toString(const SourceType _type)
	{
		return toString(_type, g_sourceTypes);
	}

	std::string toString(const TagType _type)
	{
		return toString(_type, g_tagTypes);
	}

	template<typename T>
	T fromString(const std::string& _string, const std::initializer_list<const char*>& _strings)
	{
		size_t i = 0;
		for (const auto& s : _strings)
		{
			if (s == _string)
				return static_cast<T>(i);
			++i;
		}
		return T::Invalid;
	}

	SourceType toSourceType(const std::string& _string)
	{
		return fromString<SourceType>(_string, g_sourceTypes);
	}

	TagType toTagType(const std::string& _string)
	{
		return fromString<TagType>(_string, g_tagTypes);
	}
}
