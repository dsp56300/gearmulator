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
		Parts m_parts;
		Tabs m_tabs;
	};
}