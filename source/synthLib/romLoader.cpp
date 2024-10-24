#include "romLoader.h"

#include "os.h"

namespace synthLib
{
	namespace
	{
		std::set<std::string> g_searchPaths;
	}

	std::vector<std::string> RomLoader::findFiles(const std::string& _extension, const size_t _minSize, const size_t _maxSize)
	{
		std::vector<std::string> results;

		if(g_searchPaths.empty())
		{
			g_searchPaths.insert(getModulePath(true));
			g_searchPaths.insert(getModulePath(false));
			g_searchPaths.insert(getCurrentDirectory());
		}

		for (const auto& path : g_searchPaths)
			synthLib::findFiles(results, path, _extension, _minSize, _maxSize);

		return results;
	}

	std::vector<std::string> RomLoader::findFiles(const std::string& _path, const std::string& _extension, const size_t _minSize, const size_t _maxSize)
	{
		if(_path.empty())
			return findFiles(_extension, _minSize, _maxSize);

		std::vector<std::string> results;
		synthLib::findFiles(results, _path, _extension, _minSize, _maxSize);
		return results;
	}

	void RomLoader::addSearchPath(const std::string& _path)
	{
		g_searchPaths.insert(validatePath(_path));
	}
}
