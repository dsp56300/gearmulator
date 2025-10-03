#pragma once

#include "baseLib/event.h"

#include "jucePluginEditorLib/lcd.h"

namespace jeJucePlugin
{
	class Editor;

	class JeLcd : public jucePluginEditorLib::Lcd
	{
	public:
		explicit JeLcd(Editor& _editor, Rml::Element* _parent);

		const uint8_t* getCharacterData(uint8_t _character) const override;
		bool getOverrideText(std::vector<std::string>& _lines) override;

		void setText(const std::array<char, 40>& _data);
		void setParameterDisplay(const std::string& _name, const std::string& _value);

	private:
		baseLib::EventListener<std::array<uint8_t ,64>> m_onLcdCgDataChanged;
		baseLib::EventListener<std::array<char, 40>> m_onLcdDdDataChanged;

		std::array<char, 40> m_lcdText;
	};
}
