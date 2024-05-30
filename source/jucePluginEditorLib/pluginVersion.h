#pragma once
#include <string>

namespace jucePluginEditorLib
{
	class Version
	{
	public:
		static std::string getVersionString();
		static uint32_t getVersionNumber();
		static std::string getVersionDate();
		static std::string getVersionTime();
		static std::string getVersionDateTime();
	};
}
