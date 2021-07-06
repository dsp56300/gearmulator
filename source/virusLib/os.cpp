#include "os.h"

#ifdef _WIN32
#define NOMINMAX
#define NOSERVICE
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#include "syx.h"

#pragma optimize("",off)

namespace virusLib
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
}
