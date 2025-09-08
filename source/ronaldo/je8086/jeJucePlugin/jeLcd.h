#pragma once

#include "jucePluginEditorLib/lcd.h"

namespace jeJucePlugin
{
	class JeLcd : public jucePluginEditorLib::Lcd
	{
	public:
		explicit JeLcd(Rml::Element* _parent);

		const uint8_t* getCharacterData(uint8_t _character) const override;
		bool getOverrideText(std::vector<std::string>& _lines) override;
	};
}
