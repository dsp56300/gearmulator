#pragma once

#include <string>

namespace jucePluginEditorLib
{
	struct Skin
	{
		std::string displayName;		// == folder name on disk if not embedded into binary
		std::string filename;			// name of the skin file, e.g. "skin.json" (old) or "skin.rml" (new/converted)
		std::string folder;				// empty if skin is embedded into binary
		std::vector<std::string> files;	// only valid for embedded skins, contains all files that are part of the skin

		bool operator == (const Skin& _other) const
		{
			return displayName == _other.displayName && filename == _other.filename && folder == _other.folder;
		}

		bool isValid() const
		{
			return !filename.empty();
		}
	};
}
