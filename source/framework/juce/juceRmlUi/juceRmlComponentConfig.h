#pragma once

#include <cstdint>
#include <vector>

namespace juceRmlUi
{
	enum class SoftwareRendererMode : int8_t
	{
		Auto = -1,
		ForceOff = 0,
		ForceOn = 1,
	};

	struct RmlComponentConfig
	{
		int refreshRateLimitHz = -1;
		SoftwareRendererMode forceSoftwareRenderer = SoftwareRendererMode::Auto;
		// macOS only: Metal is used whenever it is supported. Set this to fall back to OpenGL instead,
		// which is useful to find out whether a rendering problem is specific to the Metal backend.
		bool disableMetalRenderer = false;
		// Registers tus_patchmanager / tus_colorpicker / tus_settings in the document. These are
		// instantiated from C++ by name - Settings::createFromTemplate("settings") and friends - so
		// the document cannot know it will be asked for them. Skins declare the first two in their
		// own head as well; nothing declares tus_settings, so turning this off means the plugin has
		// no settings dialog. Only correct for a UI that brings its own, like the 88emu player.
		bool includeDefaultTemplates = true;

		// Adds the operating system's Japanese, Chinese and Korean fonts, and a wide-coverage one
		// where there is one, as fallbacks for text the embedded fonts cannot draw, such as file
		// names. They are memory-mapped rather than copied, so only the glyphs drawn are ever read.
		// Leave it off for a UI that only shows its own labels.
		bool systemFallbackFonts = false;

		// Registered on top of the above. Editor-driven too: pluginEditor scans the data provider
		// for per-product tus_settings_<product>.rml pages.
		std::vector<std::string> additionalTemplateFiles;
	};
}
