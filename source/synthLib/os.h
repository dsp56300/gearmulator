#pragma once

#include <string>
#include <vector>
#include <array>

namespace synthLib
{
    std::string getModulePath(bool _stripPluginComponentFolders = true);

	std::string getCurrentDirectory();
	bool createDirectory(const std::string& _dir);;
	std::string validatePath(std::string _path);

	bool getDirectoryEntries(std::vector<std::string>& _files, const std::string& _folder);

    std::string findFile(const std::string& _extension, size_t _minSize, size_t _maxSize);
    std::string findFile(const std::string& _rootPath, const std::string& _extension, size_t _minSize, size_t _maxSize);
    std::string findROM(size_t _minSize, size_t _maxSize);
	std::string findROM(size_t _expectedSize = 524288);

	bool hasExtension(const std::string& _filename, const std::string& _extension);

	void setFlushDenormalsToZero();

	bool writeFile(const std::string& _filename, const std::vector<uint8_t>& _data);
	bool writeFile(const std::string& _filename, const uint8_t* _data, size_t _size);

	template<size_t Size> bool writeFile(const std::string& _filename, const std::array<uint8_t, Size>& _data)
	{
		return writeFile(_filename, &_data[0], _data.size());
	}

	bool readFile(std::vector<uint8_t>& _data, const std::string& _filename);
} // namespace synthLib
