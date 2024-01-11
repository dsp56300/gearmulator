#pragma once

#include <functional>
#include <map>
#include <list>
#include <thread>
#include <shared_mutex>

#include "patch.h"
#include "patchdbtypes.h"
#include "search.h"

#include "jobqueue.h"

#include "juce_core/juce_core.h"

namespace pluginLib::patchDB
{
	struct SearchRequest;
	struct Patch;
	struct DataSource;

	class DB
	{
	public:
		DB(juce::File _dir);
		virtual ~DB();

		void uiProcess(Dirty& _dirty);

		DataSourceNodePtr addDataSource(const DataSource& _ds);
		void removeDataSource(const DataSource& _ds);
		void refreshDataSource(const DataSourceNodePtr& _ds);
		void renameDataSource(const DataSourceNodePtr& _ds, const std::string& _newName);

		void getDataSources(std::vector<DataSourceNodePtr>& _dataSources)
		{
			std::shared_lock lock(m_dataSourcesMutex);
			_dataSources.reserve(m_dataSources.size());
			for (const auto& it : m_dataSources)
				_dataSources.push_back(it.second);
		}

		bool addTag(TagType _type, const Tag& _tag);
		bool removeTag(TagType _type, const Tag& _tag);

		void getTags(TagType _type, std::set<Tag>& _tags);
		bool modifyTags(const std::vector<PatchPtr>& _patches, const TypedTags& _tags);

		uint32_t search(SearchRequest&& _request, std::function<void(const SearchResult&)>&& _callback);
		void cancelSearch(uint32_t _handle);
		std::shared_ptr<Search> getSearch(SearchHandle _handle);

		void copyPatchesTo(const DataSourceNodePtr& _ds, const std::vector<PatchPtr>& _patches);
		void removePatches(const DataSourceNodePtr& _ds, const std::vector<PatchPtr>& _patches);
		bool movePatchesTo(uint32_t _position, const std::vector<PatchPtr>& _patches);

		static bool isValid(const PatchPtr& _patch);

		PatchPtr requestPatchForPart(uint32_t _part);
		virtual bool requestPatchForPart(Data& _data, uint32_t _part) = 0;

	protected:
		DataSourceNodePtr addDataSource(const DataSource& _ds, bool _save);

		virtual bool loadData(DataList& _results, const DataSourceNodePtr& _ds);

		virtual bool loadRomData(DataList& _results, uint32_t _bank, uint32_t _program) = 0;
		virtual bool loadFile(DataList& _results, const std::string& _file);
		virtual bool loadLocalStorage(DataList& _results, const DataSourceNodePtr& _ds);
		virtual bool loadFolder(const DataSourceNodePtr& _folder);
		virtual PatchPtr initializePatch(const Data& _sysex) = 0;
		virtual Data prepareSave(const PatchPtr& _patch) const = 0;
		virtual bool parseFileData(DataList& _results, const Data& _data);

		void startLoaderThread();
		void stopLoaderThread();

	private:
		void runOnLoaderThread(std::function<void()>&& _func);
		void runOnUiThread(const std::function<void()>& _func);

		void addDataSource(const DataSourceNodePtr& _ds);

		bool addPatches(const std::vector<PatchPtr>& _patches);
		bool removePatch(const PatchPtr& _key);

		bool internalAddTag(TagType _type, const Tag& _tag);
		bool internalRemoveTag(TagType _type, const Tag& _tag);

		bool executeSearch(Search& _search);
		void updateSearches(const std::vector<PatchPtr>& _patches);
		bool removePatchesFromSearches(const std::vector<PatchPtr>& _keys);

		bool createConsecutiveProgramNumbers(const DataSourceNodePtr& _ds);

		bool loadJson();
		bool saveJson();
		juce::File getLocalStorageFile(const DataSource& _ds) const;
		bool saveLocalStorage() const;

		// IO
		juce::File m_settingsDir;
		juce::File m_jsonFileName;

		// loader
		JobQueue m_loader;

		// ui
		std::mutex m_uiMutex;
		std::list<std::function<void()>> m_uiFuncs;
		Dirty m_dirty;

		// data
		std::shared_mutex m_dataSourcesMutex;
		std::map<DataSource, DataSourceNodePtr> m_dataSources;	// we need a key to find duplicates, but at the same time we need pointers to do the parent relation

		std::shared_mutex m_patchesMutex;
		std::unordered_map<TagType, std::set<Tag>> m_tags;
		std::map<PatchKey, PatchModificationsPtr> m_patchModifications;

		// search
		std::shared_mutex m_searchesMutex;
		std::unordered_map<uint32_t, std::shared_ptr<Search>> m_searches;
		std::unordered_set<SearchHandle> m_cancelledSearches;
		uint32_t m_nextSearchHandle = 0;
	};
}
