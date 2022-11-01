#include "os.h"

#include "../dsp56300/source/dsp56kEmu/logging.h"

#ifndef _WIN32
// filesystem is only available on Mac OS Catalina 10.15+
// filesystem causes linker errors in gcc-8 if linked statically
#define USE_DIRENT
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
#else
#include <dlfcn.h>
#endif

#ifdef _MSC_VER
#include <cfloat>
#elif defined(HAVE_SSE)
#include <immintrin.h>
#endif

namespace synthLib
{
    std::string getModulePath()
    {
        std::string path;
#ifdef _WIN32
        char buffer[MAX_PATH];
        HMODULE hm = nullptr;

        if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCSTR>(&getModulePath), &hm) == 0)
        {
            LOG("GetModuleHandle failed, error = " << GetLastError());
            return {};
        }
        if (GetModuleFileName(hm, buffer, sizeof(buffer)) == 0)
        {
            LOG("GetModuleFileName failed, error = " << GetLastError());
            return {};
        }

        path = buffer;
#else
        Dl_info info;
        if (!dladdr(reinterpret_cast<const void *>(&getModulePath), &info))
        {
            LOG("Failed to get module path");
            return std::string();
        }
        else
        {
            const auto end = path.find_last_of("/\\");
            path = info.dli_fname;
        }
#endif

    	auto fixPathWithDelim = [&](const std::string& _key, const char _delim)
    	{
			const auto end = path.rfind(_key + _delim);

			if (end != std::string::npos && (path.find(_delim + _key) + 1) != end)
				path = path.substr(0, end);
		};

    	auto fixPath = [&](const std::string& _key)
    	{
			fixPathWithDelim(_key, '/');
			fixPathWithDelim(_key, '\\');
		};

		fixPath(".vst");
		fixPath(".vst3");
		fixPath(".component");
		fixPath(".app");

		const auto end = path.find_last_of("/\\");

        if (end != std::string::npos)
            path = path.substr(0, end + 1);

        return validatePath(path);
    }

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
        return mkdir(_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0;
#else
        return std::filesystem::create_directories(_dir);
#endif
    }

    std::string validatePath(std::string _path)
    {
        if(_path.empty())
            return _path;
        if(_path.back() == '/' || _path.back() == '\\')
            return _path;
        _path += '/';
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
                std::string file = _folder;

            	if(file.back() != '/' && file.back() != '\\')
                    file += '/';

            	file += ent->d_name;

                _files.push_back(file);
            }
            closedir(dir);
        }
        else
        {
            return false;
        }
#else
    	try
        {
            for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(_folder))
            {
                const auto &file = entry.path();

                _files.push_back(file.string());
            }
        }
        catch (...)
        {
            return false;
        }
#endif
        return !_files.empty();
    }

    static std::string lowercase(const std::string &_src)
    {
        std::string str(_src);
        for (char& i : str)
	        i = static_cast<char>(tolower(i));
        return str;
    }

    static std::string getExtension(const std::string &_name)
    {
        const auto pos = _name.find_last_of('.');
        if (pos != std::string::npos)
            return _name.substr(pos);
        return {};
    }

    std::string findFile(const std::string& _extension, const size_t _minSize, const size_t _maxSize)
    {
        std::string path = getModulePath();

        if(path.empty())
            path = getCurrentDirectory();

        std::vector<std::string> files;

        getDirectoryEntries(files, path);

        for (const auto& file : files)
        {
            if(!hasExtension(file, _extension))
                continue;

            if (!_minSize && !_maxSize)
            {
	            LOG("Found ROM at path " << file);
                return file;
            }

            FILE *hFile = fopen(file.c_str(), "rb");
            if (!hFile)
	            continue;

            fseek(hFile, 0, SEEK_END);
            const auto size = static_cast<size_t>(ftell(hFile));
            fclose(hFile);

            if (_minSize && size < _minSize)
	            continue;
            if (_maxSize && size > _maxSize)
	            continue;

            LOG("Found ROM at path " << file);

            return file;
        }
        return {};
    }

	std::string findROM(const size_t _minSize, const size_t _maxSize)
    {
	    return findFile(".bin", _minSize, _maxSize);
    }

    std::string findROM(const size_t _expectedSize)
    {
	    return findROM(_expectedSize, _expectedSize);
    }

    bool hasExtension(const std::string& _filename, const std::string& _extension)
    {
        return lowercase(getExtension(_filename)) == lowercase(_extension);
    }

    void setFlushDenormalsToZero()
    {
#if defined(_MSC_VER)
        _controlfp(_DN_FLUSH, _MCW_DN);
#elif defined(HAVE_SSE)
        _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
#endif
    }
} // namespace synthLib
