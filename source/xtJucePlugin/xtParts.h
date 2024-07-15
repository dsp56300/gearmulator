#pragma once

#include "jucePluginEditorLib/partbutton.h"

#include "xtPartButton.h"

namespace xtJucePlugin
{
	class PartName;

	class Parts
	{
	public:
		Parts(Editor& _editor);

		bool selectPart(uint8_t _part) const;

	private:
		void updateUi() const;

		Editor& m_editor;

		struct Part
		{
			PartButton* m_button = nullptr;
			PartName* m_name = nullptr;
			juce::Button* m_led = nullptr;
		};

		std::array<Part,8> m_parts;
	};
}