#include "romLoader.h"

#include "os.h"

#include "baseLib/filesystem.h"

#include <mutex>

namespace synthLib
{
	namespace
	{
		std::set<std::string> g_searchPaths;
		std::set<std::string> g_recursiveSearchPaths;

		// These are process-global, not per plugin instance. A host loading two instances at
		// once gives each its own Processor - and its own m_deviceCreateMutex - so nothing above
		// serialises them here, and two concurrent std::set inserts are a tree rebalance, not a
		// benign race. Cold path either way: this runs while a device is being created.
		std::mutex& searchPathMutex()
		{
			static std::mutex mutex;
			return mutex;
		}

		// True once a caller has named a search path of its own. Every plugin does, from its
		// constructor, before anything looks a ROM up.
		bool g_callerAddedPath = false;

		// The module directory belongs to the binary, so it is always searched. It is installed
		// lazily but must land before any caller-added path too — otherwise an addSearchPath()
		// made before the first lookup leaves the set non-empty and silently *replaces* the
		// defaults instead of adding to them.
		// Call with searchPathMutex() held.
		void ensureDefaultSearchPaths()
		{
			static bool s_initialized = false;
			if(s_initialized)
				return;
			s_initialized = true;

			g_searchPaths.insert(getModulePath(true));
			g_searchPaths.insert(getModulePath(false));
		}

		// The working directory is a fallback for the command-line tools, which are run from
		// wherever the ROMs are. Once a caller has named a path of its own there is no reason to
		// scan it: a plugin's working directory is whatever the DAW happened to leave behind, and
		// for a double-clicked application it is "/" or "C:\". Picking an unrelated file of the
		// right size out of it only produces a device that will not boot.
		// Call with searchPathMutex() held.
		void ensureLookupSearchPaths()
		{
			ensureDefaultSearchPaths();
			if(!g_callerAddedPath)
				g_searchPaths.insert(baseLib::filesystem::getCurrentDirectory());
		}
	}

	std::vector<std::string> RomLoader::findFiles(const std::string& _extension, const size_t _minSize, const size_t _maxSize)
	{
		std::vector<std::string> results;

		const std::lock_guard lock(searchPathMutex());
		ensureLookupSearchPaths();

		for (const auto& path : g_searchPaths)
			baseLib::filesystem::findFiles(results, path, _extension, _minSize, _maxSize);

		return results;
	}

	std::vector<baseLib::filesystem::FoundFile> RomLoader::findFilesRecursive(const std::string& _extension, const size_t _minSize, const size_t _maxSize)
	{
		std::vector<baseLib::filesystem::FoundFile> results;

		const std::lock_guard lock(searchPathMutex());
		ensureLookupSearchPaths();

		// Only paths the caller explicitly claimed are descended into. The rest
		// keep the flat "drop a ROM next to the executable" behaviour - including
		// the working directory on the tools-only path above, which for a
		// double-clicked application is the root of the filesystem.
		for (const auto& path : g_searchPaths)
		{
			// Depth 0 is the folder itself and nothing below it, which is the
			// flat scan; the same walk then serves both cases.
			if (g_recursiveSearchPaths.count(path))
				baseLib::filesystem::findFilesRecursive(results, path, _extension, _minSize, _maxSize);
			else
				baseLib::filesystem::findFilesRecursive(results, path, _extension, _minSize, _maxSize, 0);
		}

		return results;
	}

	std::vector<std::string> RomLoader::findFiles(const std::string& _path, const std::string& _extension, const size_t _minSize, const size_t _maxSize)
	{
		if(_path.empty())
			return findFiles(_extension, _minSize, _maxSize);

		std::vector<std::string> results;
		baseLib::filesystem::findFiles(results, _path, _extension, _minSize, _maxSize);
		return results;
	}

	void RomLoader::addSearchPath(const std::string& _path, const bool _recursive)
	{
		const std::lock_guard lock(searchPathMutex());
		ensureDefaultSearchPaths();
		g_callerAddedPath = true;
		const auto path = baseLib::filesystem::validatePath(_path);
		g_searchPaths.insert(path);
		if (_recursive)
			g_recursiveSearchPaths.insert(path);
	}
}
