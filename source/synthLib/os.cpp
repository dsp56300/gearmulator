#include "os.h"

#include "../dsp56300/source/dsp56kEmu/logging.h"

#ifdef __APPLE__			// filesystem is only available on Mac OS Catalina 10.15+
#define USE_DIRENT
#endif

#ifdef USE_DIRENT
#	include <dirent.h>
#	include <unistd.h>
#else
#	include <filesystem>
#endif

#ifdef _WIN32
#	define NOMINMAX
#	define NOSERVICE
#	include <Windows.h>
#else
#	include <dlfcn.h>
#endif

#pragma optimize("",off)

namespace synthLib
{
	std::string getModulePath()
	{
		std::string path;
#ifdef _WIN32
		char buffer[MAX_PATH];
		HMODULE hm = nullptr;

		if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(&getModulePath), &hm) == 0)
		{
		    LOG("GetModuleHandle failed, error = " << GetLastError());
			return std::string();
		}
		if (GetModuleFileName(hm, buffer, sizeof(buffer)) == 0)
		{
		    LOG("GetModuleFileName failed, error = " << GetLastError());
			return std::string();
		}

		path = buffer;
#else
		Dl_info info;
	    if (!dladdr(reinterpret_cast<const void*>(&getModulePath), &info))
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

		auto fixPath = [&](std::string key)
		{
			const auto end = path.find(key);

			if(end != std::string::npos)
				path = path.substr(0, end);
		};

		fixPath(".vst/");
		fixPath(".vst3/");
		fixPath(".component/");

		auto end = path.find_last_of("/\\");
		if(end != std::string::npos)
			path = path.substr(0, end + 1);

		return path;
	}

	static std::string lowercase(const std::string& _src)
	{
		std::string str(_src);
		for(size_t i=0; i<str.size(); ++i)
			str[i] = tolower(str[i]);
		return str;
	}

	static std::string getExtension(const std::string& _name)
	{
		const auto pos = _name.find_last_of('.');
		if(pos != std::string::npos)
			return _name.substr(pos);
		return std::string();
	}

	std::string findROM(const size_t _expectedSize)
	{
		std::string path = getModulePath();

#ifdef USE_DIRENT
		if(path.empty())
		{
			char temp[1024];
			getcwd(temp, sizeof(temp));
			path = temp;
		}

		DIR *dir;
		struct dirent *ent;
		if ((dir = opendir (path.c_str())))
		{
			/* print all the files and directories within directory */
			while ((ent = readdir (dir)))
			{
				const std::string file = path + ent->d_name;

				const std::string ext = lowercase(getExtension(file));

				if(ext != ".bin")
					continue;

				if(_expectedSize)
				{
					FILE* hFile = fopen(file.c_str(), "rb");
					if(!hFile)
						continue;

					fseek(hFile, 0, SEEK_END);
					const auto size = ftell(hFile);
					fclose(hFile);
					if(size != _expectedSize)
						continue;

					LOG("Found ROM at path " << file);

					return file;
				}
			}
			closedir (dir);
		}
		else
		{
			return std::string();
		}
#else	
		if(path.empty())
			path = std::filesystem::current_path().string();

		try
		{
			for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(path))
			{
				const auto file = entry.path();
				if(!file.has_extension())
					continue;

				if(_expectedSize && entry.file_size() != _expectedSize)
					continue;

				std::string ext = lowercase(file.extension().string());
				
				if(ext != ".bin")
					continue;

				LOG("Found ROM at path " << file);

				return file.string();
			}
		}
		catch(...)
		{
		}
#endif

		return std::string();
	}
}
