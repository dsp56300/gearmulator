#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "baseLib/event.h"

#include "synthLib/frontPanelSysex.h"

namespace Rml
{
	class Element;
}

namespace pluginLib
{
	class Controller;
}

namespace jucePluginEditorLib
{
	class Editor;

	// Binds the device's front panel (synthLib::fp frames) to the skin. The position of an element id in a list is the
	// device's index for it, a missing element is skipped. Asks the device for its state when created, keeps none
	class FrontPanel
	{
	public:
		struct Binding
		{
			// the LCD: a frame of another size is dropped. The sinks get cols x rows cells, cells 8-15 are the CGRAM
			// characters
			uint32_t lcdCols = 0;
			uint32_t lcdRows = 0;
			std::function<void(const std::vector<uint8_t>&)> lcdSink;
			std::function<void(const std::array<uint8_t, synthLib::fp::g_cgRamSize>&)> cgRamSink;

			std::vector<std::string> leds;
			std::vector<std::string> buttons;	// press and release, toggles send their state when changed
			std::vector<std::string> encoders;	// endless knobs, ids of other elements are skipped
			float encoderSpeed = 2.0f;
		};

		FrontPanel(const Editor& _editor, Binding _binding);
		~FrontPanel();

		FrontPanel(const FrontPanel&) = delete;
		FrontPanel& operator=(const FrontPanel&) = delete;

	private:
		void onEncoderChanged(size_t _index);

		pluginLib::Controller& m_controller;
		const Binding m_binding;

		std::vector<Rml::Element*> m_leds;
		std::vector<Rml::Element*> m_encoders;
		std::vector<float> m_encoderValues;

		baseLib::EventListener<uint32_t, uint32_t, std::vector<uint8_t>> m_onLcdText;
		baseLib::EventListener<std::array<uint8_t, synthLib::fp::g_cgRamSize>> m_onLcdCgRam;
		baseLib::EventListener<std::vector<bool>> m_onLedMask;
	};
}
