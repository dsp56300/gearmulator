#include "n2xMasterVolume.h"

#include "n2xController.h"
#include "n2xEditor.h"

namespace n2xJucePlugin
{
	MasterVolume::MasterVolume(const Editor& _editor) : m_editor(_editor), m_volume(_editor.findComponentT<juce::Slider>("MasterVolume"))
	{
		m_volume->setRange(0.0f, 255.0f);

		uint8_t currentValue;

		if(_editor.getN2xController().getKnobState(currentValue, n2x::KnobType::MasterVol))
			m_volume->setValue(currentValue, juce::dontSendNotification);
		else
			m_volume->setValue(255.0f, juce::dontSendNotification);

		m_volume->onValueChange = [this]
		{
			const auto sysex = n2x::State::createKnobSysex(n2x::KnobType::MasterVol, static_cast<uint8_t>(m_volume->getValue()));

			m_editor.getN2xController().sendSysEx(sysex);
		};

		m_onKnobChanged.set(_editor.getN2xController().onKnobChanged, [this](const n2x::KnobType& _type, const unsigned char& _value)
		{
			if(_type != n2x::KnobType::MasterVol)
				return;
			m_volume->setValue(_value, juce::dontSendNotification);
		});
	}
}
