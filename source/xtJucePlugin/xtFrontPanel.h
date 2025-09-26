#pragma once

#include "xtLcd.h"

#include "xtLib/xtLeds.h"

namespace juceRmlUi
{
	class ElemButton;
}

class Controller;

namespace xtJucePlugin
{
	class Editor;

	class FrontPanel
	{
	public:
		explicit FrontPanel(const Editor& _editor, Controller& _controller);
		~FrontPanel();

		void processSysex(const std::vector<uint8_t>& _msg) const;

		XtLcd* getLcd() const
		{
			return m_lcd.get();
		}

	private:
		void processLCDUpdate(const std::vector<uint8_t>& _msg) const;
		void processLedUpdate(const std::vector<uint8_t>& _msg) const;

		std::array<juceRmlUi::ElemButton*, static_cast<uint32_t>(xt::LedType::Count)> m_leds{};

		Controller& m_controller;
		std::unique_ptr<XtLcd> m_lcd;
	};
}
