#pragma once

#include <deque>
#include <functional>
#include <map>
#include <list>
#include <thread>
#include <shared_mutex>

#include "patch.h"
#include "patchdbtypes.h"
#include "search.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include "patchmodifications.h"

namespace pluginLib::patchDB
{
	struct SearchRequest;
	struct Patch;
	struct DataSource;

	class DB
	{
	public:
		DB(juce::File _json);
		virtual ~DB();

		void uiProcess(Dirty& _dirty);

		void addDataSource(const DataSource& _ds);
		void getDataSources(std::vector<DataSourcePtr>& _dataSources)
		{
			std::shared_lock lock(m_dataSourcesMutex);
			_dataSources = m_dataSources;
		}

		bool addTag(TagType _type, const std::string& _tag);
		void getTags(TagType _type, std::set<Tag>& _tags);
		bool modifyTags(const PatchPtr& _patch, const TypedTags& _tags);

		uint32_t search(SearchRequest&& _request, std::function<void(const SearchResult&)>&& _callback);
		void cancelSearch(uint32_t _handle);
		std::shared_ptr<Search> getSearch(SearchHandle _handle);

	protected:
		virtual bool loadData(DataList& _results, const DataSourcePtr& _ds);

		virtual bool loadRomData(DataList& _results, uint32_t _bank, uint32_t _program) = 0;
		virtual bool loadFile(DataList& _results, const std::string& _file);
		virtual bool loadFolder(DataList& _results, const DataSourcePtr& _folder);
		virtual PatchPtr initializePatch(const Data& _sysex, const DataSourcePtr& _ds) = 0;
		virtual bool parseFileData(DataList& _results, const Data& _data);

		void stopLoaderThread();

	private:
		void loaderThreadFunc();
		void runOnLoaderThread(const std::function<void()>& _func);
		void runOnUiThread(const std::function<void()>& _func);

		bool addDataSource(const DataSourcePtr& _ds);

		bool addPatch(const PatchPtr& _patch);
		bool removePatch(const PatchKey& _key);

		bool internalAddTag(TagType _type, const Tag& _tag);

		bool executeSearch(Search& _search);

		bool loadJson();
		bool saveJson();

		// IO
		juce::File m_jsonFileName;

		// loader
		std::unique_ptr<std::thread> m_loader;
		bool m_destroy = false;

		std::mutex m_loaderMutex;
		std::deque<std::function<void()>> m_loaderFuncs;
		std::condition_variable m_loaderCv;

		// ui
		std::mutex m_uiMutex;
		std::list<std::function<void()>> m_uiFuncs;
		Dirty m_dirty;

		// data
		std::shared_mutex m_dataSourcesMutex;
		std::vector<DataSourcePtr> m_dataSources;

		std::shared_mutex m_patchesMutex;
		std::map<PatchKey, PatchPtr> m_patches;
		std::map<TagType, std::set<Tag>> m_tags;
		std::map<PatchKey, PatchModificationsPtr> m_patchModifications;

		// search
		std::mutex m_searchesMutex;
		std::map<uint32_t, std::shared_ptr<Search>> m_searches;
		std::set<SearchHandle> m_cancelledSearches;
		uint32_t m_nextSearchHandle = 0;
	};
}
