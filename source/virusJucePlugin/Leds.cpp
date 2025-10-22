#include "Leds.h"

#include "VirusController.h"
#include "VirusEditor.h"

#include "VirusProcessor.h"

namespace genericVirusUI
{
	constexpr const char* const g_logoAnimKey = "logoAnimation";

	constexpr const char* g_lfoNames[3] = {"Lfo1LedOn", "Lfo2LedOn", "Lfo3LedOn"};

	Leds::Leds(const VirusEditor& _editor, virus::VirusProcessor& _processor) : m_processor(_processor), m_logoAnimationEnabled(_processor.getConfig().getBoolValue(g_logoAnimKey, true))
	{
		m_onFrontPanelStateChanged.set(_editor.getController().onFrontPanelStateChanged, [this](const virusLib::FrontpanelState& _frontpanelState)
		{
			onFrontPanelStateChanged(_frontpanelState);
		});

		for(size_t i=0; i<m_lfos.size(); ++i)
		{
			if(auto* comp = _editor.findChild(g_lfoNames[i], false))
			{
				m_lfos[i].reset(new jucePluginEditorLib::Led(_editor, comp));
			}
		}

		if(auto* logoAnim = _editor.findChild("logolight", false))
		{
			m_logoAnim = logoAnim;

			m_logoLed.reset(new jucePluginEditorLib::Led(_editor, logoAnim));

			addLogoClickListener(logoAnim);

			m_logo = _editor.findChild("logo", false);

			if(m_logo)
			{
				addLogoClickListener(m_logo);
			}
		}

		onFrontPanelStateChanged(_editor.getController().getFrontpanelState());
	}

	Leds::~Leds()
	{
		for (auto& led : m_lfos)
			led.reset();
	}

	void Leds::toggleLogoAnimation()
	{
		m_logoAnimationEnabled = !m_logoAnimationEnabled;

		m_processor.getConfig().setValue(g_logoAnimKey, m_logoAnimationEnabled);
		m_processor.getConfig().saveIfNeeded();
	}

	void Leds::onFrontPanelStateChanged(const virusLib::FrontpanelState& _frontpanelState) const
	{
		for(size_t i=0; i<_frontpanelState.m_lfoPhases.size(); ++i)
		{
			const auto v = std::clamp(_frontpanelState.m_lfoPhases[i], 0.0f, 1.0f);
			if(!m_lfos[i])
				continue;
			m_lfos[i]->setValue(std::pow(1.0f - v, 0.2f));
		}

		if(m_logoLed)
		{
			if(!m_logoAnimationEnabled)
			{
				m_logoLed->setValue(0.0f);
			}
			else
			{
				const auto v = std::clamp(m_processor.getModel() == virusLib::DeviceModel::Snow ? _frontpanelState.m_bpm : _frontpanelState.m_logo, 0.0f, 1.0f);

				m_logoLed->setValue(std::pow(1.0f - v, 0.2f));
			}
		}
	}

	void Leds::addLogoClickListener(Rml::Element* _logo)
	{
		juceRmlUi::EventListener::Add(_logo, Rml::EventId::Click, [this](Rml::Event& _e)
		{
			_e.StopImmediatePropagation();
			toggleLogoAnimation();
		});
	}
}
