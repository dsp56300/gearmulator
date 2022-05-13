#pragma once

#include <vector>

#include "../../juceUiLib/tabgroup.h"

namespace juce
{
	class Button;
	class Component;
}

namespace genericVirusUI
{
	class VirusEditor;

	class Tabs : genericUI::TabGroup
	{
	public:
		explicit Tabs(const VirusEditor& _editor);
	};
}
