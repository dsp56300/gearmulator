#pragma once

#include <functional>
#include <map>
#include <list>
#include <shared_mutex>

#include "patch.h"
#include "patchdbtypes.h"
#include "search.h"

#include "jobqueue.h"

#include "juce_core/juce_core.h"

namespace pluginLib
{
	class FileType;
}

namespace pluginLib::patchDB
{
	struct SearchRequest;
	struct Patch;
	struct DataSource;

	class DB
	{
	public:
		using DataSourceLoadedCallback = std::function<void(bool,DataSourceNodePtr)>;

		DB(juce::File _dir);
		virtual ~DB();

		void uiProcess();

		DataSourceNodePtr addDataSource(const DataSource& _ds, const DataSourceLoadedCallback& = [](bool, std::shared_ptr<DataSourceNode>) {});
		void removeDataSource(const DataSource& _ds, bool _save = true);
		void refreshDataSource(const DataSourceNodePtr& _ds);
		void renameDataSource(const DataSourceNodePtr& _ds, const std::string& _newName);
		DataSourceNodePtr getDataSource(const DataSource& _ds);
		std::set<DataSourceNodePtr> getDataSourcesOfSourceType(SourceType _type);

		void getDataSources(std::vector<DataSourceNodePtr>& _dataSources)
		{
			std::shared_lock lock(m_dataSourcesMutex);
			_dataSources.reserve(m_dataSources.size());
			for (const auto& it : m_dataSources)
				_dataSources.push_back(it.second);
		}

		bool setTagColor(TagType _type, const Tag& _tag, Color _color);
		Color getTagColor(TagType _type, const Tag& _tag) const;
		Color getPatchColor(const PatchPtr& _patch, const TypedTags& _tagsToIgnore) const;

		bool addTag(TagType _type, const Tag& _tag);
		bool removeTag(TagType _type, const Tag& _tag);

		void getTags(TagType _type, std::set<Tag>& _tags);
		bool modifyTags(const std::vector<PatchPtr>& _patches, const TypedTags& _tags);
		bool renamePatch(const PatchPtr& _patch, const std::string& _name);
		bool replacePatch(const PatchPtr& _existing, const PatchPtr& _new);

		SearchHandle search(SearchRequest&& _request);
		SearchHandle search(SearchRequest&& _request, SearchCallback&& _callback);
		SearchHandle findDatasourceForPatch(const PatchPtr& _patch, SearchCallback&& _callback);

		void cancelSearch(uint32_t _handle);
		std::shared_ptr<Search> getSearch(SearchHandle _handle);
		std::shared_ptr<Search> getSearch(const DataSource& _dataSource);

		void copyPatchesTo(const DataSourceNodePtr& _ds, const std::vector<PatchPtr>& _patches, int _insertRow = -1,
		                   const std::function<void(const std::vector<PatchPtr>&)>& _successCallback = {});
		void removePatches(const DataSourceNodePtr& _ds, const std::vector<PatchPtr>& _patches);
		bool movePatchesTo(uint32_t _position, const std::vector<PatchPtr>& _patches);

		static bool isValid(const PatchPtr& _patch);

		PatchPtr requestPatchForPart(uint32_t _part, uint64_t _userData = 0);
		virtual bool requestPatchForPart(Data& _data, uint32_t _part, uint64_t _userData) = 0;

		bool isLoading() const { return m_loading; }
		bool isScanning() const { return !m_loader.empty(); }

		bool writePatchesToFile(const juce::File& _file, const std::vector<PatchPtr>& _patches);

		static void assign(const PatchPtr& _patch, const PatchModificationsPtr& _mods);

		static std::string createValidFilename(const std::string& _name);

	protected:
		DataSourceNodePtr addDataSource(const DataSource& _ds, bool _save, const DataSourceLoadedCallback& = [](bool , std::shared_ptr<DataSourceNode>) {});

	public:
		virtual bool loadData(DataList& _results, const DataSourceNodePtr& _ds);
		virtual bool loadData(DataList& _results, const DataSource& _ds);

		virtual bool loadRomData(DataList& _results, uint32_t _bank, uint32_t _program) = 0;
		virtual bool loadFile(DataList& _results, const std::string& _file);
		virtual bool loadLocalStorage(DataList& _results, const DataSource& _ds);
		virtual bool loadFolder(const DataSourceNodePtr& _folder);
		virtual PatchPtr initializePatch(Data&& _sysex, const std::string& _defaultPatchName) = 0;
		virtual Data applyModifications(const PatchPtr& _patch, const FileType& _targetType) const = 0;
		virtual bool parseFileData(DataList& _results, const Data& _data);
		virtual bool equals(const PatchPtr& _a, const PatchPtr& _b) const
		{
			return _a == _b || _a->hash == _b->hash;
		}
		virtual void processDirty(const Dirty& _dirty) const = 0;

	protected:
		virtual void onLoadFinished() {}

		virtual void startLoaderThread(const juce::File& _migrateFromDir = {});
		void stopLoaderThread();

		void runOnLoaderThread(std::function<void()>&& _func);
		void runOnUiThread(const std::function<void()>& _func);

	private:
		void addDataSource(const DataSourceNodePtr& _ds);

		bool addPatches(const std::vector<PatchPtr>& _patches);
		bool removePatch(const PatchPtr& _patch);

		bool internalAddTag(TagType _type, const Tag& _tag);
		bool internalRemoveTag(TagType _type, const Tag& _tag);

		bool executeSearch(Search& _search);
		void updateSearches(const std::vector<PatchPtr>& _patches);
		bool removePatchesFromSearches(const std::vector<PatchPtr>& _keys);

		void preservePatchModifications(const PatchPtr& _patch);
		void preservePatchModifications(const std::vector<PatchPtr>& _patches);

		bool createConsecutiveProgramNumbers(const DataSourceNodePtr& _ds) const;

		Color getTagColorInternal(TagType _type, const Tag& _tag) const;

		bool loadJson();
		bool loadPatchModifications(const DataSourceNodePtr& _ds, const std::vector<PatchPtr>& _patches);
		static bool loadPatchModifications(std::map<PatchKey, PatchModificationsPtr>& _patchModifications, const juce::var& _parentNode, const DataSourceNodePtr& _dataSource = nullptr);

		bool deleteFile(const juce::File& _file);

		bool saveJson();
		bool saveJson(const DataSourceNodePtr& _ds);
		bool saveJson(const juce::File& _target, juce::DynamicObject* _src);

	public:
		juce::File getJsonFile(const DataSource& _ds) const;
		juce::File getLocalStorageFile(const DataSource& _ds) const;

	private:
		bool saveLocalStorage();

		void pushError(std::string _string);

		bool loadCache();
		void saveCache();
		juce::File getCacheFile() const;
		juce::File getJsonFile() const;

		// IO
		const juce::File m_settingsDir;

		// loader
		JobQueue m_loader;

		// ui
		std::mutex m_uiMutex;
		std::list<std::function<void()>> m_uiFuncs;
		Dirty m_dirty;

		// data
		std::shared_mutex m_dataSourcesMutex;
		std::map<DataSource, DataSourceNodePtr> m_dataSources;	// we need a key to find duplicates, but at the same time we need pointers to do the parent relation

		mutable std::shared_mutex m_patchesMutex;
		std::unordered_map<TagType, std::set<Tag>> m_tags;
		std::unordered_map<TagType, std::unordered_map<Tag, uint32_t>> m_tagColors;
		std::map<PatchKey, PatchModificationsPtr> m_patchModifications;

		// search
		std::shared_mutex m_searchesMutex;
		std::unordered_map<uint32_t, std::shared_ptr<Search>> m_searches;
		std::unordered_set<SearchHandle> m_cancelledSearches;
		uint32_t m_nextSearchHandle = 0;

		// state
		bool m_loading = true;
		bool m_cacheDirty = false;
	};
}
