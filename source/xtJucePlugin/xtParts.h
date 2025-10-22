#pragma once

#include "jucePluginEditorLib/partbutton.h"

#include "xtPartButton.h"
#include "xtPartName.h"

namespace xtJucePlugin
{
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
			std::unique_ptr<PartButton> m_button;
			std::unique_ptr<PartName> m_name;
			Rml::Element* m_led = nullptr;
		};

		std::array<Part,8> m_parts;
	};
}