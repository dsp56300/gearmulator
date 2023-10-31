#include "db.h"

#include "datasource.h"
#include "patch.h"

#include "../../synthLib/os.h"
#include "../../synthLib/midiToSysex.h"

namespace pluginLib::patchDB
{
	static void findFiles(std::vector<std::string>& _files, const std::string& _dir)
	{
		std::vector<std::string> files;
		synthLib::findFiles(files, _dir, {}, 0, 0);

		for (const auto& file : files)
		{
			FILE* hFile = fopen(file.c_str(), "rb");
			if (hFile)
			{
				fclose(hFile);
				_files.push_back(file);
			}
			else
			{
				findFiles(_files, file);
			}
		}
	}

	DB::DB()
	{
		m_loader.reset(new std::thread([&]
		{
			loaderThreadFunc();
		}));
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

	void DB::uiProcess()
	{
		std::list<std::function<void()>> uiFuncs;
		{
			std::scoped_lock lock(m_uiMutex);
			std::swap(uiFuncs, m_uiFuncs);
		}

		for (const auto& func : uiFuncs)
			func();
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
		default:
			assert(false && "unknown data source type");
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

		return true;
	}

	void DB::addCategory(const std::string& _category, const PatchKey& _key)
	{
		const auto it = m_categories.find(_category);
		if (it != m_categories.end())
			it->second.insert(_key);
		else
			m_categories.insert({ _category, {_key} });
	}
}
