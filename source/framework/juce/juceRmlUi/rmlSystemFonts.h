#pragma once

#include <memory>
#include <string>
#include <vector>

namespace Rml
{
	class CoreInstance;
}

namespace juceRmlUi::systemFonts
{
	// The operating system's font files for Japanese, Chinese and Korean, and a wide-coverage font
	// where the system has one, in fallback order. Japanese comes first so that Japanese names get
	// Japanese glyph shapes. Fonts that are not installed are left out.
	std::vector<std::string> findFallbackFontFiles();

	// Registers findFallbackFontFiles() as RmlUi fallback faces. The files are memory-mapped, not
	// copied, so only the glyphs actually drawn are ever read. The returned handles keep the
	// mappings alive; hold them until the instance has shut down. Call with RmlUi access held.
	std::vector<std::shared_ptr<const void>> loadFallbackFaces(Rml::CoreInstance& _coreInstance);
}
