#include "n2xParts.h"

#include "n2xController.h"
#include "n2xEditor.h"

namespace n2xJucePlugin
{
	Parts::Parts(Editor& _editor): m_editor(_editor)
	{
		m_parts[0] = _editor.findComponentT<Part>("PerfMidiChannelA");
		m_parts[1] = _editor.findComponentT<Part>("PerfMidiChannelB");
		m_parts[2] = _editor.findComponentT<Part>("PerfMidiChannelC");
		m_parts[3] = _editor.findComponentT<Part>("PerfMidiChannelD");

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
