#pragma once

#include <functional>
#include <map>
#include <string>

#include "datasource.h"
#include "patch.h"
#include "tags.h"

namespace pluginLib::patchDB
{
	struct SearchRequest
	{
		std::string name;
		Tags tags;
		Tags categories;
		DataSource source;

		bool match(const Patch& _patch) const;
	};

	using SearchResult = std::map<PatchKey, PatchPtr>;
	using SearchCallback = std::function<void(const SearchResult&)>;
	using SearchHandle = uint32_t;

	static constexpr SearchHandle g_invalidSearchHandle = ~0;

	enum class SearchState
	{
		NotStarted,
		Running,
		Cancelled
	};

	struct Search
	{
		SearchHandle handle = g_invalidSearchHandle;
		SearchRequest request;
		SearchCallback callback;
		SearchResult result;
		SearchState state = SearchState::NotStarted;
	};
}
