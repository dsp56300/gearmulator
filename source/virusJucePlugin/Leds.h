#pragma once

#include <array>
#include <memory>

#include "../../jucePluginEditorLib/led.h"

class AudioPluginAudioProcessor;

namespace genericUI
{
	class Editor;
}

namespace genericVirusUI
{
	class Leds
	{
	public:
		Leds(const genericUI::Editor& _editor, AudioPluginAudioProcessor& _processor);
		~Leds();

	private:
		std::array<std::unique_ptr<jucePluginEditorLib::Led>, 3> m_lfos;
		std::unique_ptr<jucePluginEditorLib::Led> m_logo;
	};
}
