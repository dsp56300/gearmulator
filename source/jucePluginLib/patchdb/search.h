#pragma once

#include <functional>
#include <map>
#include <string>
#include <shared_mutex>

#include "datasource.h"
#include "patch.h"
#include "tags.h"

namespace pluginLib::patchDB
{
	struct SearchRequest
	{
		std::string name;
		TypedTags tags;
		DataSourceNodePtr sourceNode;

		bool match(const Patch& _patch) const;
	};

	using SearchResult = std::set<PatchPtr>;
	using SearchCallback = std::function<void(const SearchResult&)>;

	enum class SearchState
	{
		NotStarted,
		Running,
		Cancelled,
		Completed
	};

	struct Search
	{
		SearchHandle handle = g_invalidSearchHandle;

		SearchRequest request;

		SearchCallback callback;

		SearchResult results;

		mutable std::shared_mutex resultsMutex;

		SearchState state = SearchState::NotStarted;

		size_t getResultSize() const
		{
			std::shared_lock searchLock(resultsMutex);
			return results.size();
		}

		SourceType getSourceType() const
		{
			if(request.sourceNode)
				return request.sourceNode->type;
			return SourceType::Invalid;
		}
	};
}
