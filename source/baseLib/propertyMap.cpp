#include "propertyMap.h"

#include <cmath>

namespace baseLib
{
	bool PropertyMap::add(const std::string& _key, const std::string& _value)
	{
		if (m_argsWithValues.find(_key) != m_argsWithValues.end())
			return false;
		m_argsWithValues.insert(std::make_pair(_key, _value));
		return true;
	}

	bool PropertyMap::add(const std::string& _key)
	{
		return m_args.insert(_key).second;
	}
	
	bool PropertyMap::add(const PropertyMap& _other, const bool _overwrite)
	{
		bool changed = false;

		for (const auto& [key, val] : _other.getArgsWithValues())
		{
			if (_overwrite || m_argsWithValues.find(key) == m_argsWithValues.end())
			{
				m_argsWithValues[key] = val;
				changed = true;
			}
		}
		for (const auto& a : _other.getArgs())
		{
			if (_overwrite || m_args.find(a) == m_args.end())
			{
				m_args.insert(a);
				changed = true;
			}
		}
		return changed;
	}

	std::string PropertyMap::tryGet(const std::string& _key, const std::string& _value) const
	{
		const auto it = m_argsWithValues.find(_key);
		if (it == m_argsWithValues.end())
			return _value;
		return it->second;
	}

	std::string PropertyMap::get(const std::string& _key, const std::string& _default/* = {}*/) const
	{
		const auto it = m_argsWithValues.find(_key);
		if (it == m_argsWithValues.end())
			return _default;
		return it->second;
	}

	float PropertyMap::getFloat(const std::string& _key, const float _default/* = 0.0f*/) const
	{
		const std::string stringResult = get(_key);

		if (stringResult.empty())
			return _default;

		const double result = atof(stringResult.c_str());
		if (std::isinf(result) || std::isnan(result))
		{
			return _default;
		}
		return static_cast<float>(result);
	}

	int PropertyMap::getInt(const std::string& _key, const int _default/* = 0*/) const
	{
		const std::string stringResult = get(_key);
		if (stringResult.empty())
			return _default;
		return atoi(stringResult.c_str());
	}

	bool PropertyMap::contains(const std::string& _key) const
	{
		return m_args.find(_key) != m_args.end() || m_argsWithValues.find(_key) != m_argsWithValues.end();
	}
}
