#include "filesystem.h"

#include <array>
#include <iostream>

#ifndef _WIN32
// filesystem is only available on macOS Catalina 10.15+
// filesystem causes linker errors in gcc-8 if linked statically
#define USE_DIRENT
#include <cstdlib>
#include <cstring>
#include <pwd.h>
#endif

#ifdef USE_DIRENT
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#else
#include <filesystem>
#endif

#ifdef _WIN32
#define NOMINMAX
#define NOSERVICE
#include <Windows.h>
#include <shlobj_core.h>
#else
#include <dlfcn.h>
#endif

#ifdef _MSC_VER
#include <cfloat>
#elif defined(HAVE_SSE)
#include <immintrin.h>
#endif

#ifdef __APPLE__
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

namespace baseLib::filesystem
{
#ifdef _WIN32
	constexpr char g_nativePathSeparator = '\\';
#else
	constexpr char g_nativePathSeparator = '/';
#endif
	constexpr char g_otherPathSeparator = g_nativePathSeparator == '\\' ? '/' : '\\';

    std::string getCurrentDirectory()
    {
#ifdef USE_DIRENT
        char temp[1024];
        getcwd(temp, sizeof(temp));
        return temp;
#else
		return std::filesystem::current_path().string();
#endif
    }

    bool createDirectory(const std::string& _dir)
    {
#ifdef USE_DIRENT
		constexpr auto dirAttribs = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
		for(size_t i=0; i<_dir.size(); ++i)
		{
			if(_dir[i] == '/' || _dir[i] == '\\')
			{
				const auto d = _dir.substr(0,i);
		        mkdir(d.c_str(), dirAttribs);
			}
		}
        return mkdir(_dir.c_str(), dirAttribs) == 0;
#else
        return std::filesystem::create_directories(_dir);
#endif
    }

    std::string validatePath(std::string _path)
    {
        if(_path.empty())
            return _path;

        for (char& ch : _path)
        {
	        if(ch == g_otherPathSeparator)
				ch = g_nativePathSeparator;
        }

		if(_path.back() == g_nativePathSeparator)
            return _path;

        _path += g_nativePathSeparator;
        return _path;
    }

    bool getDirectoryEntries(std::vector<std::string>& _files, const std::string& _folder)
    {
#ifdef USE_DIRENT
        DIR *dir;
        struct dirent *ent;
        if ((dir = opendir(_folder.c_str())))
        {
            while ((ent = readdir(dir)))
            {
				std::string f = ent->d_name;

				if(f == "." || f == "..")
					continue;

                std::string file = _folder;

            	if(file.back() != '/' && file.back() != '\\')
                    file += '/';

            	file += f;

                _files.push_back(file);
            }
            closedir(dir);
        }
        else
        {
//          LOG("Failed to open directory " << _folder << ", error " << errno);
			std::cerr << "Failed to open directory " << _folder << ", error " << errno << '\n';
            return false;
        }
#else
    	try
        {
            const auto u8Path = std::filesystem::u8path(_folder);
            for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(u8Path))
            {
                const auto &file = entry.path();

                try
                {
	                _files.push_back(file.u8string());
                }
                catch(std::exception& e)
                {
                	//LOG(e.what());
	                std::cerr << e.what() << '\n';
                }
            }
        }
        catch (std::exception& e)
        {
            //LOG(e.what());
			std::cerr << e.what() << '\n';
            return false;
        }
#endif
        return !_files.empty();
    }

	bool findFiles(std::vector<std::string>& _files, const std::string& _rootPath, const std::string& _extension, const size_t _minSize, const size_t _maxSize)
    {
        std::vector<std::string> files;

        getDirectoryEntries(files, _rootPath);

        for (const auto& file : files)
        {
            if(!hasExtension(file, _extension))
                continue;

            if (!_minSize && !_maxSize)
            {
                _files.push_back(file);
                continue;
            }

            const auto size = getFileSize(file);

            if (_minSize && size < _minSize)
	            continue;
            if (_maxSize && size > _maxSize)
	            continue;

            _files.push_back(file);
        }
        return !_files.empty();
    }

	std::string findFile(const std::string& _rootPath, const std::string& _extension, const size_t _minSize, const size_t _maxSize)
    {
        std::vector<std::string> files;
        if(!findFiles(files, _rootPath, _extension, _minSize, _maxSize))
            return {};
        return files.front();
    }


    std::string lowercase(const std::string &_src)
    {
        std::string str(_src);
        for (char& i : str)
	        i = static_cast<char>(tolower(i));
        return str;
    }

    std::string getExtension(const std::string &_name)
    {
        const auto pos = _name.find_last_of('.');
        if (pos != std::string::npos)
            return _name.substr(pos);
        return {};
    }

    std::string stripExtension(const std::string& _name)
    {
		const auto pos = _name.find_last_of('.');
		if (pos != std::string::npos)
			return _name.substr(0, pos);
		return _name;
    }

    std::string getFilenameWithoutPath(const std::string& _name)
    {
        const auto pos = _name.find_last_of("/\\");
        if (pos != std::string::npos)
            return _name.substr(pos + 1);
        return _name;
    }

    std::string getPath(const std::string& _filename)
    {
        const auto pos = _filename.find_last_of("/\\");
        if (pos != std::string::npos)
            return _filename.substr(0, pos);
        return _filename;
    }

    size_t getFileSize(const std::string& _file)
    {
        FILE* hFile = openFile(_file, "rb");
        if (!hFile)
            return 0;

        fseek(hFile, 0, SEEK_END);
        const auto size = static_cast<size_t>(ftell(hFile));
        fclose(hFile);
        return size;
    }

    bool isDirectory(const std::string& _path)
    {
#ifdef USE_DIRENT
		struct stat statbuf;
		stat(_path.c_str(), &statbuf);
		if (S_ISDIR(statbuf.st_mode))
            return true;
        return false;
#else
        return std::filesystem::is_directory(_path);
#endif
    }
    bool hasExtension(const std::string& _filename, const std::string& _extension)
    {
        if (_extension.empty())
            return true;

        return lowercase(getExtension(_filename)) == lowercase(_extension);
    }

    bool writeFile(const std::string& _filename, const std::vector<uint8_t>& _data)
    {
        return writeFile(_filename, _data.data(), _data.size());
    }

    bool writeFile(const std::string& _filename, const uint8_t* _data, size_t _size)
    {
        auto* hFile = openFile(_filename, "wb");
        if(!hFile)
            return false;
        const auto written = fwrite(&_data[0], 1, _size, hFile);
        fclose(hFile);
        return written == _size;
    }

    bool readFile(std::vector<uint8_t>& _data, const std::string& _filename)
    {
        auto* hFile = openFile(_filename, "rb");
        if(!hFile)
            return false;

    	fseek(hFile, 0, SEEK_END);
        const auto size = ftell(hFile);
        fseek(hFile, 0, SEEK_SET);

    	if(!size)
        {
	        fclose(hFile);
            _data.clear();
            return true;
        }

    	if(_data.size() != static_cast<size_t>(size))
            _data.resize(size);

    	const auto read = fread(_data.data(), 1, _data.size(), hFile);
        fclose(hFile);
        return read == _data.size();
    }

    FILE* openFile(const std::string& _name, const char* _mode)
    {
#ifdef _WIN32
        // convert filename
		std::wstring nameW = utf8ToWide(_name);
		const auto modeW = utf8ToWide(_mode);
		return _wfopen(nameW.c_str(), modeW.c_str());
#else
		return fopen(_name.c_str(), _mode);
#endif
    }

    std::string getHomeDirectory()
    {
#ifdef _WIN32
		std::array<wchar_t, MAX_PATH<<1> data;
		if (SHGetSpecialFolderPathW (nullptr, data.data(), CSIDL_PROFILE, FALSE))
			return validatePath(wideToUtf8(data.data()));

	    const auto* home = getenv("USERPROFILE");
		if (home)
			return home;

		const auto* drive = getenv("HOMEDRIVE");
		const auto* path = getenv("HOMEPATH");

		if (drive && path)
			return std::string(drive) + std::string(path);

		return "C:\\Users\\Default";			// meh, what can we do?
#else
		const char* home = getenv("HOME");
		if (home && strlen(home) > 0)
			return home;
        const auto* pw = getpwuid(getuid());
		if(pw)
			return std::string(pw->pw_dir);
		return "/tmp";							// better ideas welcome
#endif
    }

    std::string getSpecialFolderPath(const SpecialFolderType _type)
    {
#ifdef _WIN32
		std::array<wchar_t, MAX_PATH<<1> path;

		int csidl;
		switch (_type)
		{
		case SpecialFolderType::UserDocuments:
			csidl = CSIDL_PERSONAL;
			break;
		case SpecialFolderType::PrivateAppData:
			csidl = CSIDL_APPDATA;
			break;
		default:
			return {};
		}
		if (SHGetSpecialFolderPathW (nullptr, path.data(), csidl, FALSE))
			return validatePath(wideToUtf8(path.data()));
#else
		const auto h = std::getenv("HOME");
		const std::string home = validatePath(getHomeDirectory());

#if defined(__APPLE__)
		switch (_type)
		{
		case SpecialFolderType::UserDocuments:
			return home + "Documents/";
		case SpecialFolderType::PrivateAppData:
			return home + "Library/Application Support/";
		default:
			return {};
		}
#else
		// https://specifications.freedesktop.org/basedir-spec/latest/
		switch (_type)
		{
		case SpecialFolderType::UserDocuments:
			{
				const auto* docDir = std::getenv("XDG_DATA_HOME");
				if(docDir && strlen(docDir) > 0)
					return validatePath(docDir);
				return home + ".local/share/";
			}
		case SpecialFolderType::PrivateAppData:
			{
				const auto* confDir = std::getenv("XDG_CONFIG_HOME");
				if(confDir && strlen(confDir) > 0)
					return validatePath(confDir);
				return home + ".config/";
			}
		default:
			return {};
		}
#endif
#endif
		return {};
    }
#ifdef _WIN32
	std::wstring utf8ToWide(const std::string& _utf8String)
	{
		std::wstring nameW;
		nameW.resize(_utf8String.size());
		const int newSize = MultiByteToWideChar(CP_UTF8, 0, _utf8String.c_str(), static_cast<int>(_utf8String.size()), const_cast<wchar_t *>(nameW.c_str()), static_cast<int>(_utf8String.size()));
		nameW.resize(newSize);
		return nameW;
	}
	std::string wideToUtf8(const std::wstring& _wideString)
	{
		std::string name;
		name.resize(_wideString.size() * 4); // worst case, each wchar_t can be up to 4 bytes in UTF-8
		const int newSize = WideCharToMultiByte(CP_UTF8, 0, _wideString.c_str(), static_cast<int>(_wideString.size()), name.data(), static_cast<int>(name.size()), nullptr, nullptr);
		name.resize(newSize);
		return name;
	}
#endif
}
