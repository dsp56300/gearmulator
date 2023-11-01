#include "search.h"

#include "patch.h"

namespace pluginLib::patchDB
{
	namespace
	{
		std::string lowercase(const std::string& _src)
		{
			std::string str(_src);
			for (char& i : str)
				i = static_cast<char>(tolower(i));
			return str;
		}

		bool matchStringsIgnoreCase(const std::string& _test, const std::string& _search)
		{
			if (_search.empty())
				return true;

			const auto t = lowercase(_test);
			return t.find(_search) != std::string::npos;
		}

		bool testTags(const Tags& _tags, const Tags& _search)
		{
			for (const auto& t : _search.getAdded())
			{
				if (!_tags.containsAdded(t))
					return false;
			}

			for (const auto& t : _search.getRemoved())
			{
				if (_tags.containsAdded(t))
					return false;
			}
			return true;
		}
	}

	bool SearchRequest::match(const Patch& _patch) const
	{
		// datasource
		if(source.type != SourceType::Invalid && _patch.source.type != source.type)
			return false;

		if(source.bank != g_invalidBank && source.bank != _patch.source.bank != source.bank)
			return false;

		if (source.program != g_invalidProgram && source.program != _patch.source.program != source.program)
			return false;

		if (!matchStringsIgnoreCase(_patch.source.name, source.name))
			return false;

		// name
		if (!matchStringsIgnoreCase(_patch.name, name))
			return false;

		// tags & categories
		if (!testTags(_patch.categories, categories))
			return false;

		if (!testTags(_patch.tags, tags))
			return false;

		return true;
	}
}
