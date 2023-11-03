#include "db.h"

#include "datasource.h"
#include "patch.h"

#include "../../synthLib/os.h"
#include "../../synthLib/midiToSysex.h"
#include "dsp56kEmu/logging.h"

namespace pluginLib::patchDB
{
	DB::DB(juce::File _json) : m_jsonFileName(std::move(_json))
	{
		m_loader.reset(new std::thread([&]
		{
			loaderThreadFunc();
		}));

		loadJson();
	}

	DB::~DB()
	{
		assert(m_destroy && "stopLoaderThread() needs to be called by derived class in destructor");
		stopLoaderThread();
	}

	void DB::addDataSource(const DataSource& _ds)
	{
		runOnLoaderThread([this, _ds]()
		{
			const auto ds = std::make_shared<DataSource>(_ds);

			addDataSource(ds);

			saveJson();
		});
	}

	void DB::uiProcess(Dirty& _dirty)
	{
		std::list<std::function<void()>> uiFuncs;
		{
			std::scoped_lock lock(m_uiMutex);
			std::swap(uiFuncs, m_uiFuncs);
			_dirty = m_dirty;
			m_dirty = {};
		}

		for (const auto& func : uiFuncs)
			func();
	}

	uint32_t DB::search(SearchRequest&& _request, std::function<void(const SearchResult&)>&& _callback)
	{
		const auto handle = m_nextSearchHandle++;

		auto s = std::make_shared<Search>();

		s->handle = handle;
		s->request = std::move(_request);
		s->callback = std::move(_callback);

		std::scoped_lock lock(m_searchesMutex);
		m_searches.insert({ s->handle, s });

		runOnLoaderThread([this, s]
		{
			executeSearch(*s);
		});

		return handle;
	}

	void DB::cancelSearch(const uint32_t _handle)
	{
		m_cancelledSearches.insert(_handle);
	}

	std::shared_ptr<Search> DB::getSearch(const SearchHandle _handle)
	{
		std::unique_lock lock(m_searchesMutex);
		const auto it = m_searches.find(_handle);
		if (it == m_searches.end())
			return {};
		return it->second;
	}

	void DB::getCategories(std::set<Tag>& _categories)
	{
		std::shared_lock lock(m_patchesMutex);
		_categories = m_categories;
	}

	void DB::getTags(std::set<Tag>& _tags)
	{
		std::shared_lock lock(m_patchesMutex);
		_tags = m_tags;
	}

	bool DB::loadData(DataList& _results, const DataSourcePtr& _ds)
	{
		switch (_ds->type)
		{
		case SourceType::Invalid:
			return false;
		case SourceType::Rom:
			return loadRomData(_results, _ds->bank, _ds->program);
		case SourceType::File:
			return loadFile(_results, _ds->name);
		case SourceType::Folder:
			return loadFolder(_results, _ds);
		case SourceType::Count:
			return false;
//		default:
//			assert(false && "unknown data source type");
		}
		return false;
	}

	bool DB::loadFile(DataList& _results, const std::string& _file)
	{
		Data data;
		data.reserve(16384);

		const auto& file = _file;

		if (!synthLib::readFile(data, file))
			return false;

		return parseFileData(_results, data);
	}

	bool DB::loadFolder(DataList& _results, const DataSourcePtr& _folder)
	{
		assert(_folder->type == SourceType::Folder);

		std::vector<std::string> files;
		synthLib::findFiles(files, _folder->name, {}, 0, 0);

		for (const auto& file : files)
		{
			const auto child = std::make_shared<DataSource>();
			child->parent = _folder;
			child->name = file;

			if (FILE* hFile = fopen(file.c_str(), "rb"))
			{
				fclose(hFile);  // NOLINT(cert-err33-c) - why? What am I supposed to do if I cannot close a file, eh?

				child->type = SourceType::File;
			}
			else
			{
				child->type = SourceType::Folder;
			}
			addDataSource(child);
		}

		return !_results.empty();
	}

	bool DB::parseFileData(DataList& _results, const Data& _data)
	{
		synthLib::MidiToSysex::extractSysexFromData(_results, _data);
		return !_results.empty();
	}

	void DB::stopLoaderThread()
	{
		if (m_destroy)
			return;

		m_destroy = true;
		runOnLoaderThread([] {});
		m_loader->join();
		m_loader.reset();
	}

	void DB::loaderThreadFunc()
	{
		while(!m_destroy)
		{
			std::unique_lock lock(m_loaderMutex);
			m_loaderCv.wait(lock, [this] {return !m_loaderFuncs.empty(); });
			const auto func = m_loaderFuncs.front();
			m_loaderFuncs.pop_front();
			lock.unlock();

			func();
		}
	}

	void DB::runOnLoaderThread(const std::function<void()>& _func)
	{
		std::unique_lock lock(m_loaderMutex);
		m_loaderFuncs.push_back(_func);
		m_loaderCv.notify_one();
	}

	void DB::runOnUiThread(const std::function<void()>& _func)
	{
		m_uiFuncs.push_back(_func);
	}

	bool DB::addDataSource(const DataSourcePtr& _ds)
	{
		std::vector<std::vector<uint8_t>> data;

		{
			std::unique_lock lockDs(m_dataSourcesMutex);
			m_dataSources.push_back(_ds);

			std::unique_lock lockUi(m_uiMutex);
			m_dirty.dataSources = true;
		}

		if (!loadData(data, _ds))
			return false;

		bool result = false;

		for (uint32_t p = 0; p < data.size(); ++p)
		{
			if (const auto patch = initializePatch(data[p], _ds))
			{
				patch->program = p;
				result |= addPatch(patch);
			}
		}
		return result;
	}

	bool DB::addPatch(const PatchPtr& _patch)
	{
		if (!_patch)
			return false;

		if (_patch->source->type == SourceType::Invalid)
			return false;

		std::unique_lock lock(m_patchesMutex);

		const auto key = PatchKey(*_patch);

		if (m_patches.find(key) != m_patches.end())
			return false;

		m_patches.insert({key, _patch});

		// add to ongoing searches
		std::scoped_lock lockSearches(m_searchesMutex);

		for (auto& it : m_searches)
		{
			const auto& search = it.second;

			if (search->request.match(*_patch))
			{
				bool countChanged;

				{
					std::unique_lock lockResults(search->resultsMutex);
					const auto oldCount = search->results.size();
					search->results.insert({ key, _patch });
					const auto newCount = search->results.size();
					countChanged = newCount != oldCount;
				}

				if(countChanged)
				{
					std::unique_lock lockUi(m_uiMutex);
					m_dirty.searches.insert(it.first);
				}
			}
		}

		// add to all known categories
		{
			const auto oldCount = m_categories.size();
			for (const auto& c : _patch->categories.getAdded())
				m_categories.insert(c);
			const auto newCount = m_categories.size();
			if(newCount != oldCount)
			{
				std::unique_lock lockUi(m_uiMutex);
				m_dirty.categories = true;
			}
		}

		// add to all known tags
		{
			const auto oldCount = m_tags.size();
			for (const auto& c : _patch->tags.getAdded())
				m_tags.insert(c);
			const auto newCount = m_tags.size();
			if (newCount != oldCount)
			{
				std::unique_lock lockUi(m_uiMutex);
				m_dirty.tags = true;
			}
		}

		return true;
	}

	bool DB::removePatch(const PatchKey& _key)
	{
		std::unique_lock lock(m_patchesMutex);

		const auto it = m_patches.find(_key);
		if (it == m_patches.end())
			return false;

		m_patches.erase(it);

		// remove from searches
		std::scoped_lock lockSearches(m_searchesMutex);

		for (auto& itSearches : m_searches)
		{
			const auto& search = itSearches.second;

			bool countChanged;
			{
				std::unique_lock lockResults(search->resultsMutex);
				const auto oldCount = search->results.size();
				search->results.erase(_key);
				const auto newCount = search->results.size();
				countChanged = newCount != oldCount;
			}

			if(countChanged)
			{
				std::unique_lock lockUi(m_uiMutex);
				m_dirty.searches.insert(itSearches.first);
			}
		}

		std::unique_lock lockUi(m_uiMutex);
		m_dirty.patches = true;

		return true;
	}

	bool DB::executeSearch(Search& _search)
	{
		const auto oldCount = _search.getResultSize();

		std::shared_lock patchesLock(m_patchesMutex);

		for (const auto& [key, patchPtr] : m_patches)
		{
			if (m_cancelledSearches.find(_search.handle) != m_cancelledSearches.end())
				return false;

			const auto* patch = patchPtr.get();
			assert(patch);

			if(_search.request.match(*patch))
			{
				std::unique_lock searchLock(_search.resultsMutex);
				_search.results.insert({ key, patchPtr });
			}
		}

		if(_search.getResultSize() != oldCount)
		{
			std::unique_lock lockUi(m_uiMutex);
			m_dirty.searches.insert(_search.handle);
		}

		return true;
	}

	bool DB::loadJson()
	{
		bool success = true;

		const auto json = juce::JSON::parse(m_jsonFileName);
		const auto* datasources = json["datasources"].getArray();

		if(datasources)
		{
			for(int i=0; i<datasources->size(); ++i)
			{
				const auto var = datasources->getUnchecked(i);

				DataSource ds;

				ds.type = toSourceType(var["type"].toString().toStdString());
				ds.name = var["name"].toString().toStdString();

				if (ds.type != SourceType::Invalid && !ds.name.empty())
				{
					addDataSource(ds);
				}
				else
				{
					LOG("Unexpected data source type " << toString(ds.type) << " with name '" << ds.name << "'");
					success = false;
				}
			}
		}
		return success;
	}

	bool DB::saveJson()
	{
		if (!m_jsonFileName.hasWriteAccess())
			return false;
		const auto tempFile = juce::File(m_jsonFileName.getFullPathName() + "_tmp.json");
		if (!tempFile.hasWriteAccess())
			return false;

		auto* json = new juce::DynamicObject();

		std::shared_lock lockDs(m_dataSourcesMutex);
		{
			juce::Array<juce::var> dss;

			for (const auto& dataSource : m_dataSources)
			{
				if (dataSource->parent)
					continue;
				if (dataSource->type == SourceType::Rom)
					continue;

				auto* o = new juce::DynamicObject();

				o->setProperty("type", juce::String(toString(dataSource->type)));
				o->setProperty("name", juce::String(dataSource->name));

				dss.add(o);
			}
			json->setProperty("datasources", dss);
		}

		const auto jsonText = juce::JSON::toString(juce::var(json), false);
		if (!tempFile.replaceWithText(jsonText))
			return false;
		if (!tempFile.copyFileTo(m_jsonFileName))
			return false;
		tempFile.deleteFile();
		return true;
	}
}
