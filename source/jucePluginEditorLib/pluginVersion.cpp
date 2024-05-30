#include "pluginVersion.h"

#include "version.h"
#include "versionDateTime.h"

namespace jucePluginEditorLib
{
	std::string Version::getVersionString()
	{
		return g_pluginVersionString;
	}

	uint32_t Version::getVersionNumber()
	{
		return g_pluginVersion;
	}

	std::string Version::getVersionDate()
	{
		return g_pluginVersionDate;
	}

	std::string Version::getVersionTime()
	{
		return g_pluginVersionTime;
	}

	std::string Version::getVersionDateTime()
	{
		return getVersionDate() + ' ' + getVersionTime();
	}
}
