#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <array>

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
		struct FoundFile
		{
			std::string path;
			size_t size;
		};

		// As findFiles, but descends into subdirectories, and reports the size
		// it already had to look up rather than making the caller open every
		// file a second time. One stat per entry answers both "is this a
		// directory" and "how big is it".
		//
		// The traversal is bounded in depth and in entries visited because a
		// search root is not always a curated folder - it can be the current
		// working directory, which may be anything at all.
		bool findFilesRecursive(std::vector<FoundFile>& _files, const std::string& _rootPath, const std::string& _extension, size_t _minSize, size_t _maxSize, uint32_t _maxDepth = 6, size_t _maxEntries = 50000);
		std::string findFile(const std::string& _rootPath, const std::string& _extension, const size_t _minSize, const size_t _maxSize);

		bool hasExtension(const std::string& _filename, const std::string& _extension);
		size_t getFileSize(const std::string& _file);
		// An opaque stamp that changes when the file does, or 0 when it cannot be
		// stat'ed. Compare two of these for equality and nothing else: the epoch and
		// the resolution are whatever the platform gives us - st_mtime seconds since
		// 1970 where dirent is used, file_time_type ticks (100ns since 1601 on MSVC)
		// otherwise - so a difference of these is not a duration in any unit.
		uint64_t getFileModificationTime(const std::string& _file);

		bool isDirectory(const std::string& _path);

		bool writeFile(const std::string& _filename, const uint8_t* _data, size_t _size);

		template<typename Alloc>
	    bool writeFile(const std::string& _filename, const std::vector<uint8_t, Alloc>& _data)
	    {
	        return writeFile(_filename, _data.data(), _data.size());
	    }

		template<size_t Size> bool writeFile(const std::string& _filename, const std::array<uint8_t, Size>& _data)
		{
			return writeFile(_filename, &_data[0], _data.size());
		}

		bool readFile(std::vector<uint8_t>& _data, const std::string& _filename);

		template<typename T> bool readFile(T& _data, const std::string& _filename)
		{
			std::vector<uint8_t> temp;
			if (!readFile(temp, _filename))
				return false;
			_data.assign(temp.begin(), temp.end());
			return true;
		}

		FILE* openFile(const std::string& _name, const char* _mode);

		enum class SpecialFolderType : uint8_t
		{
			UserDocuments,
			PrivateAppData
		};

		std::string getHomeDirectory();
		std::string getSpecialFolderPath(SpecialFolderType _type);

#ifdef _WIN32
		std::wstring utf8ToWide(const std::string& _utf8String);
		std::string wideToUtf8(const std::wstring& _wideString);
#endif
		bool exists(const std::string& _filename);
		bool remove(const std::string& _filename);
	};
}
