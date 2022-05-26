#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <set>

namespace juce
{
	class Value;
	class Component;
}

namespace genericUI
{
	class Condition : juce::Value::Listener
	{
	public:
		Condition(juce::Component& _target, juce::Value& _value, std::set<uint8_t> _values);
		~Condition() override;
		void valueChanged(juce::Value& _value) override;
	private:
		juce::Component& m_target;
		juce::Value& m_value;
		std::set<uint8_t> m_values;
	};
}
