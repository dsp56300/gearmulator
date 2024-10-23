#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace synthLib
{
    std::string getModulePath(bool _stripPluginComponentFolders = true);

	std::string getCurrentDirectory();
	bool createDirectory(const std::string& _dir);;
	std::string validatePath(std::string _path);

	bool getDirectoryEntries(std::vector<std::string>& _files, const std::string& _folder);

	std::string lowercase(const std::string &_src);

    std::string findFile(const std::string& _extension, size_t _minSize, size_t _maxSize);
    bool findFiles(std::vector<std::string>& _files, const std::string& _rootPath, const std::string& _extension, size_t _minSize, size_t _maxSize);
    std::string findFile(const std::string& _rootPath, const std::string& _extension, size_t _minSize, size_t _maxSize);

	std::string findROM(size_t _minSize, size_t _maxSize);
	std::string findROM(size_t _expectedSize = 524288);

	std::string getExtension(const std::string& _name);
	std::string getFilenameWithoutPath(const std::string& _name);
	std::string getPath(const std::string& _filename);

	bool hasExtension(const std::string& _filename, const std::string& _extension);
	size_t getFileSize(const std::string& _file);
	bool isDirectory(const std::string& _path);

	void setFlushDenormalsToZero();

	bool writeFile(const std::string& _filename, const std::vector<uint8_t>& _data);
	bool writeFile(const std::string& _filename, const uint8_t* _data, size_t _size);

	template<size_t Size> bool writeFile(const std::string& _filename, const std::array<uint8_t, Size>& _data)
	{
		return writeFile(_filename, &_data[0], _data.size());
	}

	bool readFile(std::vector<uint8_t>& _data, const std::string& _filename);

	FILE* openFile(const std::string& _name, const char* _mode);

	bool isRunningUnderRosetta();

	enum class SpecialFolderType
	{
		UserDocuments,
		PrivateAppData
	};

	std::string getSpecialFolderPath(SpecialFolderType _type);
} // namespace synthLib
