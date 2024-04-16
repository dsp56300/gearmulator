#include "Leds.h"

#include "VirusEditor.h"

#include "../PluginProcessor.h"

namespace genericVirusUI
{
	constexpr const char* g_lfoNames[3] = {"Lfo1LedOn", "Lfo2LedOn", "Lfo3LedOn"};

	Leds::Leds(const genericUI::Editor& _editor, AudioPluginAudioProcessor& _processor)
	{
		for(size_t i=0; i<m_lfos.size(); ++i)
		{
			if(auto* comp = _editor.findComponentT<juce::Component>(g_lfoNames[i], false))
			{
				m_lfos[i].reset(new jucePluginEditorLib::Led(comp));
				m_lfos[i]->setSourceCallback([i, &_processor]
				{
					auto* d = dynamic_cast<virusLib::Device*>(_processor.getPlugin().getDevice());

					const auto v = std::clamp(d->getFrontpanelState().m_lfoPhases[i], 0.0f, 1.0f);
					return std::pow(1.0f - v, 0.2f);
				});
			}
		}

		if(auto* comp = _editor.findComponentT<juce::Component>("logolight", false))
		{
			m_logo.reset(new jucePluginEditorLib::Led(comp));

			m_logo->setSourceCallback([&_processor]
			{
				auto* d = dynamic_cast<virusLib::Device*>(_processor.getPlugin().getDevice());

				const auto& s = d->getFrontpanelState();
				
				const auto v = std::clamp(_processor.getModel() == virusLib::DeviceModel::Snow ? s.m_bpm : s.m_logo, 0.0f, 1.0f);

				return std::pow(1.0f - v, 0.2f);
			});
		}
	}

	Leds::~Leds()
	{
		for (auto& led : m_lfos)
			led.reset();
	}
}
