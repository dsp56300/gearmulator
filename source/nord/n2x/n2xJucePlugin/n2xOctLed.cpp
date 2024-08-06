#include "n2xOctLed.h"

#include "n2xController.h"
#include "n2xEditor.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace n2xJucePlugin
{
	OctLed::OctLed(Editor& _editor) : m_editor(_editor), m_octLed(_editor.findComponentT<juce::Button>("O2Pitch_LED"))
	{
		auto& c = _editor.getN2xController();

		m_onCurrentPartChanged.set(c.onCurrentPartChanged, [this](const uint8_t&)
		{
			bind();
		});

		m_octLed->setInterceptsMouseClicks(false, false);

		bind();
	}

	void OctLed::bind()
	{
		const auto& c = m_editor.getN2xController();

		m_param = c.getParameter("O2Pitch", c.getCurrentPart());

		m_onParamChanged.set(m_param, [this](const pluginLib::Parameter* _parameter)
		{
			updateStateFromParameter(_parameter);
		});

		updateStateFromParameter(m_param);
	}

	void OctLed::updateStateFromParameter(const pluginLib::Parameter* _parameter) const
	{
		const auto v = _parameter->getUnnormalizedValue();

		const bool active = v / 12 * 12 == v;

		m_octLed->setToggleState(active, juce::dontSendNotification);
	}
}
