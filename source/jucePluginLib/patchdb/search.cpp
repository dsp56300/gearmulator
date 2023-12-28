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

		bool matchDataSource(const DataSourceNode& _source, const DataSource& _search)
		{
			if (_source.hasParent() && matchDataSource(*_source.getParent(), _search))
				return true;

			if (_search.type != SourceType::Invalid && _source.type != _search.type)
				return false;

			if (_search.bank != g_invalidBank && _source.bank != _search.bank)
				return false;

			if (!matchStrings(_source.name, _search.name))
				return false;

			return true;
		}

		bool matchDataSource(const DataSourceNode* _source, const DataSourceNodePtr& _search)
		{
			if (_source == _search.get())
				return true;

			if (const auto& parent = _source->getParent())
				return matchDataSource(parent.get(), _search);

			return false;
		}
	}

	bool SearchRequest::match(const Patch& _patch) const
	{
		// datasource

		const auto patchSource = _patch.source.lock();

		if(sourceNode)
		{
			if (!matchDataSource(patchSource.get(), sourceNode))
				return false;
		}
		else if (!matchDataSource(*patchSource, source))
		{
			return false;
		}

		// name
		if (!matchStringsIgnoreCase(_patch.getName(), name))
			return false;

//		if (program != g_invalidProgram && _patch.program != program)
//			return false;

		// tags, categories, ...
		for (const auto& it : tags.get())
		{
			const auto type = it.first;
			const auto& t = it.second;

			const auto& patchTags = _patch.getTags().get(type);

			if (!testTags(patchTags, t))
				return false;
		}

		return true;
	}
}
