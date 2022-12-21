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
		Condition(juce::Component& _target, juce::Value* _value, int32_t _parameterIndex, std::set<uint8_t> _values);
		~Condition() override;
		void valueChanged(juce::Value& _value) override;
		void bind(juce::Value* _value);
		void unbind();
		int32_t getParameterIndex() const { return m_parameterIndex; }
	private:
		juce::Component& m_target;
		const int32_t m_parameterIndex;
		juce::Value* m_value = nullptr;
		std::set<uint8_t> m_values;
	};
}
