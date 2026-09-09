#include "tools.h"

#include <cstdlib>

#include "baseLib/filesystem.h"

#include "juce_audio_processors/juce_audio_processors.h"
#include "juce_gui_basics/juce_gui_basics.h"

namespace pluginLib
{
	bool Tools::isHeadless()
	{
		// returns false on a build machine without display even...
		if(juce::Desktop::getInstance().isHeadless())
			return true;

		const auto host = juce::PluginHostType::getHostPath();

		// So we use this instead. These tools cause crashes if you attempt to
		// open a message box. LV2 even opens the editor, even on a headless
		// build machine, whatever that is good for
		return host.contains("juce_vst3_helper") || host.contains("juce_lv2_helper");
	}

	std::string Tools::getPublicDataFolder(const std::string& _vendorName, const std::string& _productName)
	{
		// Escape hatch for environments where the user documents folder is not
		// usable. The build-time helpers (juce_vst3_helper & co) instantiate the
		// plugin just to read its metadata, and on the macOS build machine the
		// CI runner is a LaunchAgent, where TCC denies ~/Documents - open() then
		// blocks forever waiting for a consent dialog nobody can answer and the
		// build hangs until the job times out. Redirecting HOME does not help,
		// Xcode rewrites it for script phases, so take the folder explicitly.
		if(const auto* overrideFolder = std::getenv("TUS_DATA_FOLDER"); overrideFolder && *overrideFolder)
			return baseLib::filesystem::validatePath(baseLib::filesystem::validatePath(overrideFolder) + _vendorName + '/' + _productName + '/');

		return baseLib::filesystem::validatePath(baseLib::filesystem::getSpecialFolderPath(baseLib::filesystem::SpecialFolderType::UserDocuments) + _vendorName + '/' + _productName + '/');
	}
}
