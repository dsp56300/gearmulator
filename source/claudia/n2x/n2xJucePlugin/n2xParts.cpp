#include "n2xParts.h"

#include "n2xController.h"
#include "n2xEditor.h"

#include "jucePluginLib/parameter.h"

#include "RmlUi/Core/Element.h"

namespace n2xJucePlugin
{
	Parts::Parts(Editor& _editor): m_editor(_editor)
	{
		m_parts[0] = std::make_unique<Part>(_editor.findChild("PerfSlotActiveA"), _editor);
		m_parts[1] = std::make_unique<Part>(_editor.findChild("PerfSlotActiveB"), _editor);
		m_parts[2] = std::make_unique<Part>(_editor.findChild("PerfSlotActiveC"), _editor);
		m_parts[3] = std::make_unique<Part>(_editor.findChild("PerfSlotActiveD"), _editor);

		// lit for MIDI channel values 0-15, dimmed for 1-15
		for(uint8_t i=0; i<static_cast<uint8_t>(m_partLeds.size()); ++i)
		{
			const auto name = std::string("PerfMidiChannel") + static_cast<char>('A' + i);

			m_partLeds[i].reset(new jucePluginEditorLib::ParameterDrivenLed(_editor, name, name, 0, [](const pluginLib::Parameter& _p)
			{
				return _p.getUnnormalizedValue() <= 15;
			}, {}, [](Rml::Element& _led, const pluginLib::Parameter& _p)
			{
				const auto ch = _p.getUnnormalizedValue();
				_led.SetProperty(Rml::PropertyId::Opacity, Rml::Property(ch > 0 && ch <= 15 ? 0.5f : 1.0f, Rml::Unit::NUMBER));
			}));
		}

		for(uint8_t p=0; p<static_cast<uint8_t>(m_parts.size()); ++p)
			m_parts[p]->initalize(p);

		onCurrentPartChanged.set(_editor.getN2xController().onCurrentPartChanged, [this](const uint8_t& _part)
		{
			setCurrentPart(_part);
		});

		setCurrentPart(0);
	}

	void Parts::setCurrentPart(const uint8_t _part) const
	{
		juce::MessageManager::callAsync([this, _part]
		{
			for(uint8_t p=0; p<static_cast<uint8_t>(m_parts.size()); ++p)
			{
				m_parts[p]->setChecked(_part == p);
			}
		});
	}
}
