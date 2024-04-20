#include "Tabs.h"

#include "VirusEditor.h"

namespace genericVirusUI
{
	Tabs::Tabs(const VirusEditor& _editor)
		: genericUI::TabGroup("pages"
			, {"page_osc", "page_lfo", "page_fx", "page_arp", "page_presets"}
			, {"TabOsc", "TabLfo", "TabEffects", "TabArp", "Presets"}
		)
	{
		create(_editor);
	}
}
