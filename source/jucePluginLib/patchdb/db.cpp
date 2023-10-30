#include "db.h"

#include "datasource.h"
#include "patch.h"

namespace pluginLib::patchDB
{
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
			std::vector<uint8_t> data;
			if(loadData(data, _ds))
			{
				if(const auto p = initializePatch(data, _ds))
					addPatch(p);
			}
		});
	}

	bool DB::loadData(std::vector<uint8_t>& _result, const DataSource& _ds)
	{
		switch (_ds.type)
		{
		case SourceType::Invalid:
			return false;
		case SourceType::Rom:
			return loadRomData(_result, _ds.bank, _ds.program);
		case SourceType::File:
			break;
		case SourceType::Folder:
			break;
		default:
			assert(false && "unknown data source type");
		}
		return false;
	}

	void DB::loaderThreadFunc()
	{
		while(!m_destroy)
		{
			const auto func = m_loaderFuncs.pop_front();
			func();
		}
	}

	void DB::runOnLoaderThread(const std::function<void()>& _func)
	{
		m_loaderFuncs.push_back(_func);
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

		for (const auto& c : _patch->categories)
		{
			addCategory(c, key);
		}
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
