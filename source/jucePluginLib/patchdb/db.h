#pragma once

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

	protected:
		virtual bool loadData(std::vector<uint8_t>& _result, const DataSource& _ds);

		virtual bool loadRomData(std::vector<uint8_t>&, uint32_t _bank, uint32_t _program) = 0;
		virtual std::shared_ptr<Patch> initializePatch(const std::vector<uint8_t>& _sysex, const DataSource& _ds) = 0;

	private:
		void loaderThreadFunc();
		void runOnLoaderThread(const std::function<void()>& _func);
		bool addPatch(const PatchPtr& _patch);
		void addCategory(const std::string& _category, const PatchKey& _key);

		std::unique_ptr<std::thread> m_loader;
		bool m_destroy = false;

		dsp56k::RingBuffer<std::function<void()>, 256, true> m_loaderFuncs;

		std::shared_mutex m_patchesMutex;
		std::map<PatchKey, PatchPtr> m_patches;
		std::map<std::string, std::set<PatchKey>> m_categories;
		std::map<std::string, std::set<PatchKey>> m_tags;
	};
}
