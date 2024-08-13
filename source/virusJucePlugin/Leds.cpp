#include "Leds.h"

#include "VirusEditor.h"

#include "VirusProcessor.h"

namespace genericVirusUI
{
	constexpr const char* const g_logoAnimKey = "logoAnimation";

	constexpr const char* g_lfoNames[3] = {"Lfo1LedOn", "Lfo2LedOn", "Lfo3LedOn"};

	Leds::Leds(const genericUI::Editor& _editor, virus::VirusProcessor& _processor) : m_processor(_processor), m_logoAnimationEnabled(_processor.getConfig().getBoolValue(g_logoAnimKey, true))
	{
		for(size_t i=0; i<m_lfos.size(); ++i)
		{
			if(auto* comp = _editor.findComponentT<juce::Component>(g_lfoNames[i], false))
			{
				m_lfos[i].reset(new jucePluginEditorLib::Led(comp));
				m_lfos[i]->setSourceCallback([i, &_processor]
				{
					auto* d = dynamic_cast<virusLib::Device*>(_processor.getPlugin().getDevice());

					if(!d)
						return 0.0f;

					const auto v = std::clamp(d->getFrontpanelState().m_lfoPhases[i], 0.0f, 1.0f);
					return std::pow(1.0f - v, 0.2f);
				});
			}
		}

		if(auto* logoAnim = _editor.findComponentT<juce::Component>("logolight", false))
		{
			m_logoAnim = logoAnim;

			m_logoLed.reset(new jucePluginEditorLib::Led(logoAnim));

			m_logoLed->setSourceCallback([this, &_processor]
			{
				if(!m_logoAnimationEnabled)
					return 0.0f;
				auto* d = dynamic_cast<virusLib::Device*>(_processor.getPlugin().getDevice());

				if(!d)
					return 0.0f;

				const auto& s = d->getFrontpanelState();
				
				const auto v = std::clamp(_processor.getModel() == virusLib::DeviceModel::Snow ? s.m_bpm : s.m_logo, 0.0f, 1.0f);

				return std::pow(1.0f - v, 0.2f);
			});

			m_logoClickListener.reset(new LogoMouseListener(*this));

			m_logoAnim->addMouseListener(m_logoClickListener.get(), false);
			m_logoAnim->setInterceptsMouseClicks(true, true);

			m_logo = _editor.findComponent("logo", false);

			if(m_logo)
			{
				m_logo->addMouseListener(m_logoClickListener.get(), false);
				m_logo->setInterceptsMouseClicks(true, true);
			}
		}
	}

	Leds::~Leds()
	{
		for (auto& led : m_lfos)
			led.reset();

		if(m_logo)
			m_logo->removeMouseListener(m_logoClickListener.get());
		if(m_logoAnim)
			m_logoAnim->removeMouseListener(m_logoClickListener.get());

		m_logoClickListener.reset();
	}

	void Leds::toggleLogoAnimation()
	{
		m_logoAnimationEnabled = !m_logoAnimationEnabled;

		m_processor.getConfig().setValue(g_logoAnimKey, m_logoAnimationEnabled);
		m_processor.getConfig().saveIfNeeded();
	}
}
