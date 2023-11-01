#pragma once

#include <deque>
#include <functional>
#include <map>
#include <thread>
#include <shared_mutex>

#include "patch.h"
#include "patchdbtypes.h"
#include "search.h"
#include "dsp56kEmu/ringbuffer.h"

namespace pluginLib::patchDB
{
	struct SearchRequest;
	struct Patch;
	struct DataSource;

	class DB
	{
	public:
		struct Dirty
		{
			bool categories = false;
			bool tags = false;
			std::set<SearchHandle> searches;
			bool patches;
		};

		DB();
		virtual ~DB();

		void addDataSource(const DataSource& _ds);

		void uiProcess(Dirty& _dirty);

		uint32_t search(SearchRequest&& _request, std::function<void(const SearchResult&)>&& _callback);
		void cancelSearch(uint32_t _handle);

	protected:
		virtual bool loadData(DataList& _results, const DataSource& _ds);

		virtual bool loadRomData(DataList& _results, uint32_t _bank, uint32_t _program) = 0;
		virtual std::shared_ptr<Patch> initializePatch(const Data& _sysex, const DataSource& _ds) = 0;
		virtual bool parseFileData(DataList& _results, const Data& _data);

	private:
		void loaderThreadFunc();
		void runOnLoaderThread(const std::function<void()>& _func);
		void runOnUiThread(const std::function<void()>& _func);

		bool addPatch(const PatchPtr& _patch);
		bool removePatch(const PatchKey& _key);

		bool executeSearch(Search& _search);

		std::unique_ptr<std::thread> m_loader;
		bool m_destroy = false;

		// loader
		std::mutex m_loaderMutex;
		std::deque<std::function<void()>> m_loaderFuncs;
		std::condition_variable m_loaderCv;

		// ui
		std::mutex m_uiMutex;
		std::list<std::function<void()>> m_uiFuncs;
		Dirty m_dirty;

		// data
		std::shared_mutex m_patchesMutex;
		std::map<PatchKey, PatchPtr> m_patches;
		std::set<Tag> m_tags;
		std::set<Tag> m_categories;

		// search
		std::mutex m_searchesMutex;
		std::map<uint32_t, Search> m_searches;
		std::set<SearchHandle> m_cancelledSearches;
		uint32_t m_nextSearchHandle = 0;
	};
}
