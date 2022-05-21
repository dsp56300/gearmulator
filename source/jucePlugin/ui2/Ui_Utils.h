#pragma once

#include "Virus_LookAndFeel.h"

#include "../../virusLib/microcontrollerTypes.h"

namespace Trancy
{
	using namespace VirusUI;
	constexpr auto knobSize = LookAndFeel::kKnobSize;
	constexpr auto knobSizeSmall = LookAndFeelSmallButton::kKnobSize;

	using namespace virusLib;

	static void setupBackground(juce::Component &parent, std::unique_ptr<juce::Drawable> &bg, const void *data,
								const size_t numBytes)
	{
		bg = juce::Drawable::createFromImageData(data, numBytes);
		parent.addAndMakeVisible(bg.get());
		bg->setBounds(bg->getDrawableBounds().toNearestIntEdges());
	}

	static void setupRotary(juce::Component &parent, juce::Slider &slider)
	{
		slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
		slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
		parent.addAndMakeVisible(slider);
	}
	static juce::String getCurrentPartBankStr(virusLib::BankNumber currentBank) 
	{ 
		switch (currentBank)
		{
			case virusLib::BankNumber::A: return "A";
			case virusLib::BankNumber::B: return "B";
			case virusLib::BankNumber::C: return "C";
			case virusLib::BankNumber::D: return "D";
			case virusLib::BankNumber::E: return "E";
			case virusLib::BankNumber::F: return "F";
			case virusLib::BankNumber::G: return "G";
			case virusLib::BankNumber::H: return "H";
			case virusLib::BankNumber::EditBuffer: return "EB";
		}

		return "ERR"; 
	}

	static PresetVersion guessVersion(const uint8_t* _data)
	{
		if (_data)
			return static_cast<PresetVersion>(_data[0]);
		return PresetVersion::A;
	}
} // namespace Trancy