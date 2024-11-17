#pragma once

#include <string>

namespace jucePluginEditorLib
{
	struct Skin
	{
		std::string displayName;	// == folder name on disk if not embedded into binary
		std::string jsonFilename;
		std::string folder;			// empty if skin is embedded into binary

		bool operator == (const Skin& _other) const
		{
			return displayName == _other.displayName && jsonFilename == _other.jsonFilename && folder == _other.folder;
		}

		bool isValid() const
		{
			return !jsonFilename.empty();
		}
	};
}
