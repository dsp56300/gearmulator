#pragma once

#include <memory>

#include "mqLcdBase.h"

namespace jucePluginEditorLib
{
	class FrontPanel;
}

namespace mqJucePlugin
{
	class Editor;

	// The microQ panel: the LCD, a canvas or, in skins without one, two text lines, and the element ids of its LEDs,
	// buttons and encoders in the order of mqLib's Leds::Led, Buttons::ButtonType and Buttons::Encoders
	class FrontPanel
	{
	public:
		explicit FrontPanel(const Editor& _editor);
		~FrontPanel();

	private:
		std::unique_ptr<MqLcdBase> m_lcd;
		std::unique_ptr<jucePluginEditorLib::FrontPanel> m_panel;
	};
}
