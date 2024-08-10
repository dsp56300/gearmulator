#include "n2xParts.h"

#include "n2xController.h"
#include "n2xEditor.h"

namespace n2xJucePlugin
{
	Parts::Parts(Editor& _editor): m_editor(_editor)
	{
		m_parts[0] = _editor.findComponentT<Part>("PerfSlotActiveA");
		m_parts[1] = _editor.findComponentT<Part>("PerfSlotActiveB");
		m_parts[2] = _editor.findComponentT<Part>("PerfSlotActiveC");
		m_parts[3] = _editor.findComponentT<Part>("PerfSlotActiveD");

		m_partLeds[0].reset(new PartLed(_editor, 0));
		m_partLeds[1].reset(new PartLed(_editor, 1));
		m_partLeds[2].reset(new PartLed(_editor, 2));
		m_partLeds[3].reset(new PartLed(_editor, 3));

		for(uint8_t p=0; p<static_cast<uint8_t>(m_parts.size()); ++p)
		{
			auto* part = m_parts[p];
			part->initalize(p);
		}

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
				auto* part = m_parts[p];
				part->setToggleState(_part == p, juce::dontSendNotification);
			}
		});
	}
}
