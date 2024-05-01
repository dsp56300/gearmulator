#include "romLoader.h"

#include "os.h"

namespace synthLib
{

	std::vector<std::string> RomLoader::findFiles(const std::string& _extension, const size_t _minSize, const size_t _maxSize)
	{
		std::vector<std::string> results;

        const auto path = synthLib::getModulePath();
		synthLib::findFiles(results, path, _extension, _minSize, _maxSize);

    	const auto path2 = synthLib::getModulePath(false);
		if(path2 != path)
			synthLib::findFiles(results, path2, _extension, _minSize, _maxSize);

		if(results.empty())
		{
            const auto path3 = synthLib::getCurrentDirectory();
			if(path3 != path2 && path3 != path)
				synthLib::findFiles(results, path, _extension, _minSize, _maxSize);
		}

		return results;
	}

	std::vector<std::string> RomLoader::findFiles(const std::string& _path, const std::string& _extension, size_t _minSize, size_t _maxSize)
	{
		if(_path.empty())
			return findFiles(_extension, _minSize, _maxSize);

		std::vector<std::string> results;
		synthLib::findFiles(results, _path, _extension, _minSize, _maxSize);
		return results;
	}
}
