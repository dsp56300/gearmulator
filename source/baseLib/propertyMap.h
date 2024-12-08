#pragma once

#include <string>
#include <map>
#include <set>

namespace baseLib
{
	class PropertyMap
	{
	public:
		bool add(const std::string& _key, const std::string& _value);
		bool add(const std::string& _key);
		bool add(const PropertyMap& _other, bool _overwrite = false);

		std::string tryGet(const std::string& _key, const std::string& _value = std::string()) const;

		std::string get(const std::string& _key, const std::string& _default = {}) const;

		float getFloat(const std::string& _key, float _default = 0.0f) const;

		int getInt(const std::string& _key, int _default = 0) const;

		bool contains(const std::string& _key) const;

		const auto& getArgsWithValues() const { return m_argsWithValues; }
		const auto& getArgs() const { return m_args; }

		bool empty() const { return m_argsWithValues.empty() && m_args.empty(); }

	private:
		std::map<std::string, std::string> m_argsWithValues;
		std::set<std::string> m_args;
	};
}
