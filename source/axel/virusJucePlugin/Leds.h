#pragma once

#include <array>
#include <memory>

#include "baseLib/event.h"

#include "jucePluginEditorLib/led.h"

#include "virusLib/frontpanelState.h"

namespace juce
{
	class MouseListener;
}

namespace virus
{
	class VirusProcessor;
}

namespace genericUI
{
	class Editor;
}

namespace genericVirusUI
{
	class VirusEditor;

	class Leds
	{
	public:
		Leds(const VirusEditor& _editor, virus::VirusProcessor& _processor);
		~Leds();

		void toggleLogoAnimation();

		bool supportsLogoAnimation() const { return m_logoLed.get(); }
		bool isLogoAnimationEnabled() const { return m_logoAnimationEnabled; }

	private:
		void onFrontPanelStateChanged(const virusLib::FrontpanelState& _frontpanelState) const;
		void addLogoClickListener(Rml::Element* _logo);

		virus::VirusProcessor& m_processor;
		bool m_logoAnimationEnabled = true;

		std::array<std::unique_ptr<jucePluginEditorLib::Led>, 3> m_lfos;

		std::unique_ptr<jucePluginEditorLib::Led> m_logoLed;

		Rml::Element* m_logo = nullptr;
		Rml::Element* m_logoAnim = nullptr;

		baseLib::EventListener<virusLib::FrontpanelState> m_onFrontPanelStateChanged;
	};
}
