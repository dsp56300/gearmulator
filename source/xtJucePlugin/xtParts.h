#pragma once

#include "../jucePluginEditorLib/partbutton.h"

#include "xtPartButton.h"

namespace xtJucePlugin
{
	class Parts
	{
	public:
		Parts(Editor& _editor);

		bool selectPart(uint8_t _part);

	private:
		void updateUi();

		Editor& m_editor;

		struct Part
		{
			PartButton* m_button = nullptr;
			juce::Button* m_led = nullptr;
		};

		std::array<Part,8> m_parts;
	};
}