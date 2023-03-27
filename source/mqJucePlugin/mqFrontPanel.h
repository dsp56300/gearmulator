#pragma once

#include "../mqLib/buttons.h"
#include "../mqLib/leds.h"

#include "mqLcdBase.h"

#include <juce_audio_processors/juce_audio_processors.h>

class Controller;

namespace mqJucePlugin
{
	class Editor;

	class FrontPanel
	{
	public:
		explicit FrontPanel(const Editor& _editor, Controller& _controller);
		~FrontPanel();

		void processSysex(const std::vector<uint8_t>& _msg) const;

	private:
		void processLCDUpdate(const std::vector<uint8_t>& _msg) const;
		void processLCDCGRamUpdate(const std::vector<uint8_t>& _msg) const;
		void processLedUpdate(const std::vector<uint8_t>& _msg) const;

		void onButtonStateChanged(uint32_t _index) const;
		void onEncoderValueChanged(uint32_t _index);

		std::array<juce::Button*, static_cast<uint32_t>(mqLib::Leds::Led::Count)> m_leds{};
		std::array<juce::Button*, static_cast<uint32_t>(mqLib::Buttons::ButtonType::Count)> m_buttons{};
		std::array<juce::Slider*, static_cast<uint32_t>(mqLib::Buttons::Encoders::Count)> m_encoders{};
		std::array<float, static_cast<uint32_t>(mqLib::Buttons::Encoders::Count)> m_encoderValues{};

		Controller& m_controller;
		std::unique_ptr<MqLcdBase> m_lcd;
	};
}
