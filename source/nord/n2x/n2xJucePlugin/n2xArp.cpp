#include "n2xArp.h"

#include "n2xController.h"
#include "n2xEditor.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace n2xJucePlugin
{
	Arp::Arp(Editor& _editor) : m_editor(_editor), m_btArpActive(_editor.findComponentT<juce::Button>("ArpEnabled"))
	{
		auto& c = _editor.getN2xController();

		m_onCurrentPartChanged.set(c.onCurrentPartChanged, [this](const uint8_t&)
		{
			bind();
		});

		m_btArpActive->onClick = [this]
		{
			if(!m_param)
				return;
			m_param->setUnnormalizedValueNotifyingHost(m_btArpActive->getToggleState() ? 0 : 3, pluginLib::Parameter::Origin::Ui);
		};

		bind();
	}

	void Arp::bind()
	{
		const auto& c = m_editor.getN2xController();

		m_param = c.getParameter("Lfo2Dest", c.getCurrentPart());

		m_onParamChanged.set(m_param, [this](const pluginLib::Parameter* _parameter)
		{
			updateStateFromParameter(_parameter);
		});

		updateStateFromParameter(m_param);
	}

	void Arp::updateStateFromParameter(const pluginLib::Parameter* _parameter) const
	{
		const auto v = _parameter->getUnnormalizedValue();

		const bool arpActive = v != 3 && v != 4 && v != 7;

		m_btArpActive->setToggleState(arpActive, juce::dontSendNotification);
	}
}
