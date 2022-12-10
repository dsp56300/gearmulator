#pragma once

#include <memory>
#include <vector>

namespace juce
{
	class Button;
	class Slider;
}

namespace genericUI
{
	class ControllerLink;
}

namespace genericVirusUI
{
	class VirusEditor;

	class ControllerLinks
	{
	public:
		ControllerLinks(const VirusEditor& _editor);

	private:
		void createLink(juce::Slider* _a, juce::Slider* _b, juce::Button* _cond);

		std::vector<std::unique_ptr<genericUI::ControllerLink>> m_links;
	};
}
