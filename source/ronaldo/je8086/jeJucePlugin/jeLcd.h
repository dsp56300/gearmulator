#pragma once

#include "baseLib/event.h"

#include "jucePluginEditorLib/lcd.h"

namespace juceRmlUi
{
	class ElemButton;
}

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

		void renamePerformance() const;

	private:
		void sendPerfPatchToggle(Rml::Event& _event, bool _pressed) const;

		baseLib::EventListener<std::array<uint8_t ,64>> m_onLcdCgDataChanged;
		baseLib::EventListener<std::array<char, 40>> m_onLcdDdDataChanged;

		Editor& m_editor;
		std::array<char, 40> m_lcdText;
		juceRmlUi::ElemButton* m_btPerfPatch = nullptr;
	};
}
