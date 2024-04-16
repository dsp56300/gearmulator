#include "led.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib
{
	Led::Led(juce::Component* _targetAlpha, juce::Component* _targetInvAlpha)
	: m_targetAlpha(_targetAlpha)
	, m_targetInvAlpha(_targetInvAlpha)
	{
	}

	void Led::setValue(const float _v)
	{
		if(m_value == _v)  // NOLINT(clang-diagnostic-float-equal)
			return;

		m_value = _v;

		repaint();
	}

	void Led::timerCallback()
	{
		setValue(m_sourceCallback());
	}

	void Led::setSourceCallback(SourceCallback&& _func, const int _timerMilliseconds/* = 1000/60*/)
	{
		m_sourceCallback = std::move(_func);

		if(m_sourceCallback)
			startTimer(_timerMilliseconds);
		else
			stopTimer();
	}

	void Led::repaint() const
	{
		m_targetAlpha->setOpaque(false);
		m_targetAlpha->setAlpha(m_value);

		if(!m_targetInvAlpha)
			return;

		m_targetInvAlpha->setOpaque(false);
		m_targetInvAlpha->setAlpha(1.0f - m_value);
	}
}
