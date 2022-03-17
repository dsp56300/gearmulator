#pragma once
#include <string>
#include <vector>

namespace synthLib
{
    std::string getModulePath();

	std::string getCurrentDirectory();
	bool createDirectory(const std::string& _dir);;

	bool getDirectoryEntries(std::vector<std::string>& _files, const std::string& _folder);

    std::string findROM(size_t _minSize, size_t _maxSize);
	std::string findROM(size_t _expectedSize = 524288);

	void setFlushDenormalsToZero();
} // namespace synthLib
