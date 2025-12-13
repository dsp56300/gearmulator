#pragma once

#include <string>

namespace synthLib
{
    std::string getModulePath(bool _stripPluginComponentFolders = true);

	std::string findROM(size_t _minSize, size_t _maxSize);
}
