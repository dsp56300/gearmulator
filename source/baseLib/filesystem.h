#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace baseLib
{
	namespace filesystem
	{
		std::string getCurrentDirectory();
		bool createDirectory(const std::string& _dir);
		std::string validatePath(std::string _path);

		std::string lowercase(const std::string &_src);

		std::string getExtension(const std::string& _name);
		std::string stripExtension(const std::string& _name);
		std::string getFilenameWithoutPath(const std::string& _name);
		std::string getPath(const std::string& _filename);

		bool getDirectoryEntries(std::vector<std::string>& _files, const std::string& _folder);

		bool findFiles(std::vector<std::string>& _files, const std::string& _rootPath, const std::string& _extension, size_t _minSize, size_t _maxSize);
		std::string findFile(const std::string& _rootPath, const std::string& _extension, const size_t _minSize, const size_t _maxSize);

		bool hasExtension(const std::string& _filename, const std::string& _extension);
		size_t getFileSize(const std::string& _file);

		bool isDirectory(const std::string& _path);

		bool writeFile(const std::string& _filename, const std::vector<uint8_t>& _data);
		bool writeFile(const std::string& _filename, const uint8_t* _data, size_t _size);

		template<size_t Size> bool writeFile(const std::string& _filename, const std::array<uint8_t, Size>& _data)
		{
			return writeFile(_filename, &_data[0], _data.size());
		}

		bool readFile(std::vector<uint8_t>& _data, const std::string& _filename);

		FILE* openFile(const std::string& _name, const char* _mode);

		enum class SpecialFolderType : uint8_t
		{
			UserDocuments,
			PrivateAppData
		};

		std::string getHomeDirectory();
		std::string getSpecialFolderPath(SpecialFolderType _type);
	};
}
