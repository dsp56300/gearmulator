#pragma once

#include <deque>
#include <functional>
#include <map>
#include <set>
#include <thread>
#include <shared_mutex>

#include "patch.h"
#include "patchdbtypes.h"
#include "dsp56kEmu/ringbuffer.h"

namespace pluginLib::patchDB
{
	struct Patch;
	struct DataSource;

	class DB
	{
	public:

		DB();
		virtual ~DB();

		void addDataSource(const DataSource& _ds);

		void uiProcess();

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
		void addCategory(const std::string& _category, const PatchKey& _key);

		std::unique_ptr<std::thread> m_loader;
		bool m_destroy = false;

		std::mutex m_loaderMutex;
		std::deque<std::function<void()>> m_loaderFuncs;
		std::condition_variable m_loaderCv;

		std::mutex m_uiMutex;
		std::list<std::function<void()>> m_uiFuncs;

		std::shared_mutex m_patchesMutex;
		std::map<PatchKey, PatchPtr> m_patches;
		std::map<std::string, std::set<PatchKey>> m_categories;
		std::map<std::string, std::set<PatchKey>> m_tags;
	};
}
