#pragma once

#include "juce_data_structures/juce_data_structures.h"

#include <set>

namespace juce
{
	class Value;
	class Component;
}

namespace genericUI
{
	class Editor;

	class Condition
	{
	public:
		Condition(juce::Component& _target) : m_target(_target) {}
		virtual ~Condition() = default;

		virtual void setCurrentPart(const Editor& _editor, uint8_t _part) {}
		virtual void refresh() {}
		void setEnabled(bool _enable);
		bool isEnabled() const { return m_enabled; }

	private:
		juce::Component& m_target;
		bool m_enabled = false;
	};

	class ConditionByParameterValues : public Condition, juce::Value::Listener
	{
	public:
		ConditionByParameterValues(juce::Component& _target, juce::Value* _value, int32_t _parameterIndex, std::set<uint8_t> _values);
		~ConditionByParameterValues() override;
		void valueChanged(juce::Value& _value) override;
		void bind(juce::Value* _value);
		void unbind();
		int32_t getParameterIndex() const { return m_parameterIndex; }
		void refresh() override;
		void setCurrentPart(const Editor& _editor, uint8_t _part) override;

	private:
		const int32_t m_parameterIndex;
		juce::Value* m_value = nullptr;
		std::set<uint8_t> m_values;
	};

	class ConditionByKeyValue : public Condition
	{
	public:
		ConditionByKeyValue(juce::Component& _target, std::string&& _key, std::set<std::string>&& _values) : Condition(_target), m_key(std::move(_key)), m_values(std::move(_values))
		{
		}

		void setValue(const std::string& _value);

		const std::string& getKey() const { return m_key; }

	private:
		const std::string m_key;
		const std::set<std::string> m_values;
	};
}
