#pragma once

#include <string>

namespace jucePluginEditorLib
{
	struct Skin
	{
		std::string displayName;
		std::string jsonFilename;
		std::string folder;

		bool operator == (const Skin& _other) const
		{
			return displayName == _other.displayName && jsonFilename == _other.jsonFilename && folder == _other.folder;
		}
	};
}
