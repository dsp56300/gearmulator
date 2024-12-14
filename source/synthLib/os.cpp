#include "os.h"

#include "baseLib/filesystem.h"

#include "dsp56kEmu/logging.h"

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

using namespace baseLib::filesystem;

namespace synthLib
{
	std::string getModulePath(bool _stripPluginComponentFolders/* = true*/)
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
            path = info.dli_fname;
        }
#endif

    	auto fixPathWithDelim = [&](const std::string& _key, const char _delim)
    	{
			const auto end = path.rfind(_key + _delim);

            // strip folders such as "/foo.vst/" but do NOT strip "/.vst/"
			if (end != std::string::npos && (path.find(_delim + _key) + 1) != end)
				path = path.substr(0, end);
		};

    	auto fixPath = [&](const std::string& _key)
    	{
			fixPathWithDelim(_key, '/');
			fixPathWithDelim(_key, '\\');
		};

        if(_stripPluginComponentFolders)
        {
			fixPath(".vst");
			fixPath(".vst3");
			fixPath(".clap");
			fixPath(".component");
			fixPath(".app");
        }

		const auto end = path.find_last_of("/\\");

        if (end != std::string::npos)
            path = path.substr(0, end + 1);

        return validatePath(path);
    }

	namespace
	{
	    std::string findFile(const std::string& _extension, const size_t _minSize, const size_t _maxSize, const bool _stripPluginComponentFolders)
	    {
	        std::string path = getModulePath(_stripPluginComponentFolders);

	        if(path.empty())
	            path = getCurrentDirectory();

	        return baseLib::filesystem::findFile(path, _extension, _minSize, _maxSize);
	    }
	}

    std::string findFile(const std::string& _extension, const size_t _minSize, const size_t _maxSize)
    {
        auto res = findFile(_extension, _minSize, _maxSize, true);
		if (!res.empty())
			return res;
		return findFile(_extension, _minSize, _maxSize, false);
    }

	std::string findROM(const size_t _minSize, const size_t _maxSize)
    {
        std::string path = getModulePath();

        if(path.empty())
            path = getCurrentDirectory();

		auto f = baseLib::filesystem::findFile(path, ".bin", _minSize, _maxSize);
        if(!f.empty())
            return f;

    	path = getModulePath(false);

		return baseLib::filesystem::findFile(path, ".bin", _minSize, _maxSize);
    }

    std::string findROM(const size_t _expectedSize)
    {
	    return findROM(_expectedSize, _expectedSize);
    }

    void setFlushDenormalsToZero()
    {
#if defined(_MSC_VER)
        _controlfp(_DN_FLUSH, _MCW_DN);
#elif defined(HAVE_SSE)
        _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
#endif
    }

    bool isRunningUnderRosetta()
    {
#ifdef __APPLE__
		int ret = 0;
		size_t size = sizeof(ret);
		if (sysctlbyname("sysctl.proc_translated", &ret, &size, NULL, 0) == -1) 
		{
			if (errno == ENOENT)
				return false;	// no, native
			return false;		// unable to tell, assume native
		}
		return ret == 1;		// Rosetta if result is 1
#else
		return false;
#endif
   	}
} // namespace synthLib
