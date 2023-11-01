#include "db.h"

#include "datasource.h"
#include "patch.h"

#include "../../synthLib/os.h"
#include "../../synthLib/midiToSysex.h"

namespace pluginLib::patchDB
{
	namespace
	{
		void findFiles(std::vector<std::string>& _files, const std::string& _dir)
		{
			std::vector<std::string> files;
			synthLib::findFiles(files, _dir, {}, 0, 0);

			for (const auto& file : files)
			{
				if (FILE* hFile = fopen(file.c_str(), "rb"))
				{
					fclose(hFile);  // NOLINT(cert-err33-c) - why? What am I supposed to do if I cannot close a file, eh?
					_files.push_back(file);
				}
				else
				{
					findFiles(_files, file);
				}
			}
		}
	}

	DB::DB()
	{
		m_loader.reset(new std::thread([&]
		{
			loaderThreadFunc();
		}));

		DataSource ds;
		ds.type = SourceType::Folder;
		ds.name = "f:\\AccessVirusEmulator\\Presets";
		addDataSource(ds);
	}

	DB::~DB()
	{
		m_destroy = true;
		runOnLoaderThread([] {});
		m_loader->join();
		m_loader.reset();
	}

	void DB::addDataSource(const DataSource& _ds)
	{
		runOnLoaderThread([this, _ds]()
		{
			std::vector<std::vector<uint8_t>> data;
			if(loadData(data, _ds))
			{
				for(uint32_t p=0; p<data.size(); ++p)
				{
					if (const auto patch = initializePatch(data[p], _ds))
					{
						patch->program = p;
						addPatch(patch);
					}
				}
			}
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

		Search s;

		s.handle = handle;
		s.request = std::move(_request);
		s.callback = std::move(_callback);

		runOnLoaderThread([this, s]
		{
			Search search = s;
			if(executeSearch(search))
			{
				std::scoped_lock lock(m_searchesMutex);
				m_searches.insert({ search.handle, search });
			}
		});

		return s.handle;
	}

	void DB::cancelSearch(const uint32_t _handle)
	{
		m_cancelledSearches.insert(_handle);
	}

	bool DB::loadData(DataList& _results, const DataSource& _ds)
	{
		switch (_ds.type)
		{
		case SourceType::Invalid:
			return false;
		case SourceType::Rom:
			return loadRomData(_results, _ds.bank, _ds.program);
		case SourceType::File:
			break;
		case SourceType::Folder:
			{
				std::vector<std::string> files;
				findFiles(files, _ds.name);

				Data data;
				data.reserve(16384);

				for (const auto& file : files)
				{
					if(!synthLib::readFile(data, file))
						continue;

					DataList results;
					if (parseFileData(results, data))
						_results.insert(_results.end(), results.begin(), results.end());
				}

				return !_results.empty();
			}
//		default:
//			assert(false && "unknown data source type");
		}
		return false;
	}

	bool DB::parseFileData(DataList& _results, const Data& _data)
	{
		synthLib::MidiToSysex::extractSysexFromData(_results, _data);
		return !_results.empty();
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

	bool DB::addPatch(const PatchPtr& _patch)
	{
		if (!_patch)
			return false;

		if (_patch->source.type == SourceType::Invalid)
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
			auto& search = it.second;

			if (search.request.match(*_patch))
			{
				const auto oldCount = search.result.size();
				search.result.insert({ key, _patch });
				const auto newCount = search.result.size();
				if(newCount > oldCount)
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
			auto& search = itSearches.second;
			search.result.erase(_key);
		}

		std::unique_lock lockUi(m_uiMutex);
		m_dirty.patches = true;

		return true;
	}

	bool DB::executeSearch(Search& _search)
	{
		const auto oldCount = _search.result.size();

		for (const auto& [key, patchPtr] : m_patches)
		{
			if (m_cancelledSearches.find(_search.handle) != m_cancelledSearches.end())
				return false;

			const auto* patch = patchPtr.get();
			assert(patch);

			if(_search.request.match(*patch))
				_search.result.insert({ key, patchPtr });
		}

		if(_search.result.size() != oldCount)
		{
			std::unique_lock lockUi(m_uiMutex);
			m_dirty.searches.insert(_search.handle);
		}
		return true;
	}
}
