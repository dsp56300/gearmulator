#pragma once

#include "../genericUI/editor.h"

#include "Parts.h"
#include "Tabs.h"

namespace genericVirusUI
{
	class VirusEditor : public genericUI::Editor
	{
	public:
		VirusEditor(VirusParameterBinding& _binding, Virus::Controller& _controller);

	private:
		void onProgramChange();

		Parts m_parts;
		Tabs m_tabs;

		juce::Label* m_presetName = nullptr;
	};
}
