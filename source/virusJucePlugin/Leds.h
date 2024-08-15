#pragma once

#include <array>
#include <memory>

#include "jucePluginEditorLib/led.h"

#include "juce_gui_basics/juce_gui_basics.h"

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
	class Leds
	{
	public:
		class LogoMouseListener final : public juce::MouseListener
		{
		public:
			LogoMouseListener(Leds& _leds) : m_leds(_leds)
			{
			}
			void mouseDown(const juce::MouseEvent& e) override
			{
				if(e.mods.isPopupMenu())
					return;
				m_leds.toggleLogoAnimation();
			}

		private:
			Leds& m_leds;
		};

		Leds(const genericUI::Editor& _editor, virus::VirusProcessor& _processor);
		~Leds();

		void toggleLogoAnimation();

		bool supportsLogoAnimation() const { return m_logoLed.get(); }
		bool isLogoAnimationEnabled() const { return m_logoAnimationEnabled; }

	private:
		virus::VirusProcessor& m_processor;
		bool m_logoAnimationEnabled = true;

		std::array<std::unique_ptr<jucePluginEditorLib::Led>, 3> m_lfos;

		std::unique_ptr<jucePluginEditorLib::Led> m_logoLed;

		juce::Component* m_logo = nullptr;
		juce::Component* m_logoAnim = nullptr;

		std::unique_ptr<LogoMouseListener> m_logoClickListener;
	};
}
