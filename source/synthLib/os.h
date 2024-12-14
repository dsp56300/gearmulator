#pragma once

#include <string>

namespace synthLib
{
    std::string getModulePath(bool _stripPluginComponentFolders = true);

    std::string findFile(const std::string& _extension, size_t _minSize, size_t _maxSize);

	std::string findROM(size_t _minSize, size_t _maxSize);
	std::string findROM(size_t _expectedSize = 524288);

	void setFlushDenormalsToZero();

	bool isRunningUnderRosetta();
}
