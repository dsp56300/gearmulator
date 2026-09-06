#pragma once

#include "mqLib/buttons.h"
#include "mqLib/leds.h"

#include "mqLcdBase.h"
#include "juceRmlUi/rmlElemKnob.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include "synthLib/midiTypes.h"

namespace juceRmlUi
{
	class ElemButton;
}

namespace mqJucePlugin
{
	class Controller;
	class Editor;

	class FrontPanel
	{
	public:
		explicit FrontPanel(const Editor& _editor, Controller& _controller);
		~FrontPanel();

		void processSysex(const synthLib::SysexBuffer& _msg) const;

	private:
		void processLCDUpdate(const synthLib::SysexBuffer& _msg) const;
		void processLCDCGRamUpdate(const synthLib::SysexBuffer& _msg) const;
		void processLedUpdate(const synthLib::SysexBuffer& _msg) const;

		void onButtonStateChanged(uint32_t _index) const;
		void onEncoderValueChanged(uint32_t _index);

		std::array<Rml::Element*, static_cast<uint32_t>(mqLib::Leds::Led::Count)> m_leds{};
		std::array<Rml::Element*, static_cast<uint32_t>(mqLib::Buttons::ButtonType::Count)> m_buttons{};
		std::array<Rml::Element*, static_cast<uint32_t>(mqLib::Buttons::Encoders::Count)> m_encoders{};
		std::array<float, static_cast<uint32_t>(mqLib::Buttons::Encoders::Count)> m_encoderValues{};

		Controller& m_controller;
		std::unique_ptr<MqLcdBase> m_lcd;
	};
}
