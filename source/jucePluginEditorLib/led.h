#pragma once

#include "juce_events/juce_events.h"

namespace juce
{
	class Component;
}

namespace jucePluginEditorLib
{
	class Led : public juce::Timer
	{
	public:
		using SourceCallback = std::function<float()>;

		Led(juce::Component* _targetAlpha, juce::Component* _targetInvAlpha = nullptr);

		void setValue(float _v);

		void timerCallback() override;

		void setSourceCallback(SourceCallback&& _func, int _timerMilliseconds = 1000/60);

	private:
		void repaint() const;

		juce::Component* m_targetAlpha;
		juce::Component* m_targetInvAlpha;

		float m_value = -1.0f;

		SourceCallback m_sourceCallback;
	};
}
