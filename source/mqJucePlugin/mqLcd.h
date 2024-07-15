#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

#include "mqLcdBase.h"

#include "jucePluginEditorLib/lcd.h"

class MqLcd final : public jucePluginEditorLib::Lcd, public MqLcdBase
{
public:
	explicit MqLcd(Component& _parent);
	~MqLcd() override;

	void setText(const std::array<uint8_t, 40> &_text) override;
	void setCgRam(std::array<uint8_t, 64>& _data) override;

	bool getOverrideText(std::vector<std::vector<uint8_t>>& _lines) override;
	const uint8_t* getCharacterData(uint8_t _character) const override;
};
