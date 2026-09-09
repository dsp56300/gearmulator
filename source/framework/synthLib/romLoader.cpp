#include "romLoader.h"

#include "os.h"

#include "baseLib/filesystem.h"

namespace synthLib
{
	namespace
	{
		std::set<std::string> g_searchPaths;
		std::set<std::string> g_recursiveSearchPaths;

		// The built-in locations (module directory + current directory) are
		// installed lazily, but they must be installed before any caller-added
		// path too — otherwise an addSearchPath() made before the first lookup
		// leaves the set non-empty and silently *replaces* the defaults instead
		// of adding to them.
		void ensureDefaultSearchPaths()
		{
			static bool s_initialized = false;
			if(s_initialized)
				return;
			s_initialized = true;

			g_searchPaths.insert(getModulePath(true));
			g_searchPaths.insert(getModulePath(false));
			g_searchPaths.insert(baseLib::filesystem::getCurrentDirectory());
		}
	}

	std::vector<std::string> RomLoader::findFiles(const std::string& _extension, const size_t _minSize, const size_t _maxSize)
	{
		std::vector<std::string> results;

		ensureDefaultSearchPaths();

		for (const auto& path : g_searchPaths)
			baseLib::filesystem::findFiles(results, path, _extension, _minSize, _maxSize);

		return results;
	}

	std::vector<baseLib::filesystem::FoundFile> RomLoader::findFilesRecursive(const std::string& _extension, const size_t _minSize, const size_t _maxSize)
	{
		std::vector<baseLib::filesystem::FoundFile> results;

		ensureDefaultSearchPaths();

		// Only paths the caller explicitly claimed are descended into. The rest
		// keep the flat "drop a ROM next to the executable" behaviour: the
		// working directory is one of them, and for a double-clicked
		// application that is the root of the filesystem.
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
		ensureDefaultSearchPaths();
		const auto path = baseLib::filesystem::validatePath(_path);
		g_searchPaths.insert(path);
		if (_recursive)
			g_recursiveSearchPaths.insert(path);
	}
}
