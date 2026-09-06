#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "juce_core/juce_core.h"

namespace mcpServer
{
	// Thin JSON wrapper around juce::var for MCP protocol usage
	class JsonValue
	{
	public:
		JsonValue() = default;
		explicit JsonValue(const juce::var& _v) : m_var(_v) {}

		static JsonValue object() { return JsonValue(juce::var(new juce::DynamicObject())); }
		static JsonValue array() { return JsonValue(juce::var(juce::Array<juce::var>())); }
		static JsonValue null() { return JsonValue(juce::var()); }
		static JsonValue fromString(const juce::String& _s) { return JsonValue(juce::var(_s)); }
		static JsonValue fromInt(int _i) { return JsonValue(juce::var(_i)); }
		static JsonValue fromInt64(int64_t _i) { return JsonValue(juce::var(static_cast<juce::int64>(_i))); }
		static JsonValue fromDouble(double _d) { return JsonValue(juce::var(_d)); }
		static JsonValue fromBool(bool _b) { return JsonValue(juce::var(_b)); }

		void set(const juce::String& _key, const JsonValue& _value)
		{
			if (auto* obj = m_var.getDynamicObject())
				obj->setProperty(_key, _value.m_var);
		}

		void append(const JsonValue& _value)
		{
			if (auto* arr = m_var.getArray())
				arr->add(_value.m_var);
		}

		JsonValue get(const juce::String& _key) const
		{
			if (const auto* obj = m_var.getDynamicObject())
				return JsonValue(obj->getProperty(_key));
			return {};
		}

		JsonValue getArrayElement(int _index) const
		{
			if (const auto* arr = m_var.getArray())
				if (_index >= 0 && _index < arr->size())
					return JsonValue((*arr)[_index]);
			return {};
		}

		int getArraySize() const
		{
			if (const auto* arr = m_var.getArray())
				return arr->size();
			return 0;
		}

		juce::String getString() const { return m_var.toString(); }
		int getInt() const { return static_cast<int>(m_var); }
		int64_t getInt64() const { return static_cast<int64_t>(static_cast<juce::int64>(m_var)); }
		double getDouble() const { return static_cast<double>(m_var); }
		bool getBool() const { return static_cast<bool>(m_var); }

		bool isVoid() const { return m_var.isVoid(); }
		bool isObject() const { return m_var.getDynamicObject() != nullptr; }
		bool isArray() const { return m_var.isArray(); }
		bool isString() const { return m_var.isString(); }
		bool isInt() const { return m_var.isInt() || m_var.isInt64(); }
		bool isDouble() const { return m_var.isDouble(); }
		bool isBool() const { return m_var.isBool(); }

		const juce::var& getVar() const { return m_var; }

		bool hasProperty(const juce::String& _key) const
		{
			if (const auto* obj = m_var.getDynamicObject())
				return obj->hasProperty(_key);
			return false;
		}

		std::string toJsonString() const
		{
			return juce::JSON::toString(m_var, true).toStdString();
		}

		static JsonValue parse(const std::string& _json)
		{
			auto result = juce::JSON::parse(juce::String(_json));
			return JsonValue(result);
		}

	private:
		juce::var m_var;
	};
}
