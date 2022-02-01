#include "Virus_Buttons.h"
#include "BinaryData.h"
#include "Virus_LookAndFeel.h"

using namespace juce;

namespace Buttons
{

	Buttons::Button1::Button1() : DrawableButton("Button1", DrawableButton::ImageRaw)
	{
		auto off = Drawable::createFromImageData(BinaryData::btn_1_png, BinaryData::btn_1_pngSize);
		auto on = off->createCopy();
		on->setOriginWithOriginalSize({0, 2});
		setColour(DrawableButton::ColourIds::backgroundColourId, Colours::transparentBlack);
		setColour(DrawableButton::ColourIds::backgroundOnColourId, Colours::transparentBlack);
		setClickingTogglesState(true);
		on->setOriginWithOriginalSize({0, -kHeight});
		setImages(off.get(), nullptr, on.get(), nullptr, on.get());
	}

	Buttons::Button2::Button2() : DrawableButton("Button2", DrawableButton::ImageRaw)
	{
		auto off = Drawable::createFromImageData(BinaryData::btn_2_png, BinaryData::btn_2_pngSize);
		auto on = off->createCopy();
		on->setOriginWithOriginalSize({0, 2});
		setColour(DrawableButton::ColourIds::backgroundColourId, Colours::transparentBlack);
		setColour(DrawableButton::ColourIds::backgroundOnColourId, Colours::transparentBlack);
		setClickingTogglesState(true);
		on->setOriginWithOriginalSize({0, -kHeight});
		setImages(off.get(), nullptr, on.get(), nullptr, on.get());
	}

    Buttons::Button3::Button3() : DrawableButton("Button3", DrawableButton::ImageRaw)
	{
		auto off = Drawable::createFromImageData(BinaryData::btn_3_png, BinaryData::btn_3_pngSize);
		auto on = off->createCopy();
		on->setOriginWithOriginalSize({0, 2});
		setColour(DrawableButton::ColourIds::backgroundColourId, Colours::transparentBlack);
		setColour(DrawableButton::ColourIds::backgroundOnColourId, Colours::transparentBlack);
		setClickingTogglesState(true);
		on->setOriginWithOriginalSize({0, -kHeight});
		setImages(off.get(), nullptr, on.get(), nullptr, on.get());
	}

    Buttons::Button4::Button4() : DrawableButton("Button4", DrawableButton::ImageRaw)
	{
		auto off = Drawable::createFromImageData(BinaryData::btn_4_png, BinaryData::btn_4_pngSize);
		auto on = off->createCopy();
		on->setOriginWithOriginalSize({0, 2});
		setColour(DrawableButton::ColourIds::backgroundColourId, Colours::transparentBlack);
		setColour(DrawableButton::ColourIds::backgroundOnColourId, Colours::transparentBlack);
		setClickingTogglesState(true);
		on->setOriginWithOriginalSize({0, -kHeight});
		setImages(off.get(), nullptr, on.get(), nullptr, on.get());
	}
	
	Buttons::ButtonMenu::ButtonMenu() : DrawableButton("ButtonMenu", DrawableButton::ImageRaw)
	{
		auto normal = Drawable::createFromImageData(BinaryData::btn_menu_png, BinaryData::btn_menu_pngSize);
		auto pressed = normal->createCopy();
        pressed->setOriginWithOriginalSize({0, 2});
		setColour(DrawableButton::ColourIds::backgroundColourId, Colours::transparentBlack);
		setColour(DrawableButton::ColourIds::backgroundOnColourId, Colours::transparentBlack);
		setImages(normal.get(), nullptr, pressed.get(), nullptr, pressed.get(), nullptr, normal.get());
	}

    Buttons::PresetButtonLeft::PresetButtonLeft() : DrawableButton("PresetButtonLeft", DrawableButton::ImageRaw)
    {
        auto normal = Drawable::createFromImageData(BinaryData::btn_left_png, BinaryData::btn_left_pngSize);
        auto pressed = normal->createCopy();
        pressed->setOriginWithOriginalSize({0, 2});
        setColour(DrawableButton::ColourIds::backgroundColourId, Colours::transparentBlack);
        setColour(DrawableButton::ColourIds::backgroundOnColourId, Colours::transparentBlack);
        setImages(normal.get(), nullptr, pressed.get(), nullptr, pressed.get(), nullptr, normal.get());
    }

    Buttons::PresetButtonRight::PresetButtonRight() : DrawableButton("PresetButtonLeft", DrawableButton::ImageRaw)
    {
        auto normal = Drawable::createFromImageData(BinaryData::btn_right_png, BinaryData::btn_right_pngSize);
        auto pressed = normal->createCopy();
        pressed->setOriginWithOriginalSize({0, 2});
        setColour(DrawableButton::ColourIds::backgroundColourId, Colours::transparentBlack);
        setColour(DrawableButton::ColourIds::backgroundOnColourId, Colours::transparentBlack);
        setImages(normal.get(), nullptr, pressed.get(), nullptr, pressed.get(), nullptr, normal.get());
    }

    Buttons::PresetButtonDown::PresetButtonDown() : DrawableButton("PresetButtonDown", DrawableButton::ImageRaw)
    {
        auto normal = Drawable::createFromImageData(BinaryData::btn_down_png, BinaryData::btn_down_pngSize);
        auto pressed = normal->createCopy();
        pressed->setOriginWithOriginalSize({0, 2});
        setColour(DrawableButton::ColourIds::backgroundColourId, Colours::transparentBlack);
        setColour(DrawableButton::ColourIds::backgroundOnColourId, Colours::transparentBlack);
        setImages(normal.get(), nullptr, pressed.get(), nullptr, pressed.get(), nullptr, normal.get());
    }

    Buttons::OptionButtonLoadBank::OptionButtonLoadBank() : DrawableButton("LoadBank", DrawableButton::ImageRaw)
    {
        auto normal = Drawable::createFromImageData(BinaryData::btn_load_bank_png, BinaryData::btn_load_bank_pngSize);
        auto pressed = normal->createCopy();
        pressed->setOriginWithOriginalSize({0, 2});
        setColour(DrawableButton::ColourIds::backgroundColourId, Colours::transparentBlack);
        setColour(DrawableButton::ColourIds::backgroundOnColourId, Colours::transparentBlack);
        setImages(normal.get(), nullptr, pressed.get(), nullptr, pressed.get(), nullptr, normal.get());
    }

    Buttons::OptionButtonSavePreset::OptionButtonSavePreset() : DrawableButton("LoadBank", DrawableButton::ImageRaw)
    {
        auto normal = Drawable::createFromImageData(BinaryData::btn_save_preset_png, BinaryData::btn_save_preset_pngSize);
        auto pressed = normal->createCopy();
        pressed->setOriginWithOriginalSize({0, 2});
        setColour(DrawableButton::ColourIds::backgroundColourId, Colours::transparentBlack);
        setColour(DrawableButton::ColourIds::backgroundOnColourId, Colours::transparentBlack);
        setImages(normal.get(), nullptr, pressed.get(), nullptr, pressed.get(), nullptr, normal.get());
    }
}; // namespace Buttons
