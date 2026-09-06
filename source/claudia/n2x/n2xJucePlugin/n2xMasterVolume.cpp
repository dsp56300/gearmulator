#include "n2xMasterVolume.h"

#include "n2xController.h"
#include "n2xEditor.h"
#include "juceRmlUi/rmlElemValue.h"

namespace n2xJucePlugin
{
	MasterVolume::MasterVolume(const Editor& _editor) : m_editor(_editor), m_volume(_editor.findChild("MasterVolume"))
	{
		juceRmlUi::ElemValue::setRange(m_volume, 0, 255);

		uint8_t currentValue;

		if(_editor.getN2xController().getKnobState(currentValue, n2x::KnobType::MasterVol))
			juceRmlUi::ElemValue::setValue(m_volume, currentValue, false);
		else
			juceRmlUi::ElemValue::setValue(m_volume, 255.0f, juce::dontSendNotification);

		juceRmlUi::EventListener::Add(m_volume, Rml::EventId::Change, [this](Rml::Event& _event)
		{
			juce::MessageManager::callAsync([this]()
			{
				const auto sysex = n2x::State::createKnobSysex(n2x::KnobType::MasterVol, static_cast<uint8_t>(juceRmlUi::ElemValue::getValue(m_volume)));
				m_editor.getN2xController().sendSysEx(sysex);
			});
		});

		m_onKnobChanged.set(_editor.getN2xController().onKnobChanged, [this](const n2x::KnobType& _type, const unsigned char& _value)
		{
			if(_type != n2x::KnobType::MasterVol)
				return;
			juceRmlUi::ElemValue::setValue(m_volume, _value, false);
		});
	}
}
