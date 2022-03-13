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
		case virusLib::BankNumber::A:
			return "A";
		case virusLib::BankNumber::B:
			return "B";
		case virusLib::BankNumber::C:
			return "C";
		case virusLib::BankNumber::D:
			return "D";
		case virusLib::BankNumber::E:
			return "E";
		case virusLib::BankNumber::F:
			return "F";
		case virusLib::BankNumber::G:
			return "G";
		case virusLib::BankNumber::H:
			return "H";
		case virusLib::BankNumber::EditBuffer:
			return "EB";
		}

		return "ERR";
	}

	static VirusModel guessVersion(uint8_t *data)
	{
		if (data[51] > 3)
		{
			// check extra filter modes
			return VirusModel::C;
		}
		if (data[179] == 0x40 && data[180] == 0x40) // soft knobs don't exist on B so they have fixed value
		{
			return VirusModel::B;
		}
		/*if (data[232] != 0x03 || data[235] != 0x6c || data[238] != 0x01) { // extra mod slots
			return VirusModel::C;
		}*/
		/*if(data[173] != 0x00 || data[174] != 0x00) // EQ
			return VirusModel::C;*/
		/*if (data[220] != 0x40 || data[221] != 0x54 || data[222] != 0x20 || data[223] != 0x40 || data[224] != 0x40) {
			// eq controls
			return VirusModel::C;
		}*/
		// return VirusModel::C;
	}
} // namespace Trancy