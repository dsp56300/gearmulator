#pragma once

#include <functional>
#include <string>
#include <shared_mutex>

#include "datasource.h"
#include "tags.h"

namespace pluginLib::patchDB
{
	struct Search;

	struct SearchRequest
	{
		std::string name;
		TypedTags tags;
		DataSourceNodePtr sourceNode;
		PatchPtr patch;	// used by the UI to restore selection of a patch, the data source of this request patch will be null, the result will tell the UI which datasource it is in
		SourceType sourceType = SourceType::Invalid;

		bool match(const Patch& _patch) const;
		bool isValid() const;
		bool operator == (const SearchRequest& _r) const;
	};

	using SearchResult = std::set<PatchPtr>;
	using SearchCallback = std::function<void(const Search&)>;

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
			return request.sourceType;
		}

		void setCompleted()
		{
			state = SearchState::Completed;

			if(!callback)
				return;

			std::shared_lock searchLock(resultsMutex);
			callback(*this);
		}
	};
}
