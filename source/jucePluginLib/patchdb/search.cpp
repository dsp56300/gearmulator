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

		bool matchStrings(const std::string& _test, const std::string& _search)
		{
			if (_search.empty())
				return true;

			return _test.find(_search) != std::string::npos;
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

		bool matchDataSource(const DataSource& _source, const DataSource& _search)
		{
			if (_source.parent && matchDataSource(*_source.parent, _search))
				return true;

			if (_search.type != SourceType::Invalid && _source.type != _search.type)
				return false;

			if (_search.bank != g_invalidBank && _source.bank != _search.bank)
				return false;

			if (_search.program != g_invalidProgram && _source.program != _search.program)
				return false;

			if (!matchStrings(_source.name, _search.name))
				return false;

			return true;
		}
	}

	bool SearchRequest::match(const Patch& _patch) const
	{
		// datasource
		if (!matchDataSource(*_patch.source, source))
			return false;

		// name
		if (!matchStringsIgnoreCase(_patch.name, name))
			return false;

		// tags, categories, ...
		for (const auto& it : tags.get())
		{
			const auto type = it.first;
			const auto& t = it.second;

			const auto& patchTags = _patch.tags.get(type);

			if (!testTags(patchTags, t))
				return false;
		}

		return true;
	}
}
