#pragma once

#include <string>
#include <map>
#include <set>

namespace baseLib
{
	class CommandLine
	{
	public:
		CommandLine(int _argc, char* _argv[]);

		std::string tryGet(const std::string& _key, const std::string& _value = std::string()) const;

		std::string get(const std::string& _key, const std::string& _default = {}) const;

		float getFloat(const std::string& _key, float _default = 0.0f) const;

		int getInt(const std::string& _key, int _default = 0) const;

		bool contains(const std::string& _key) const;

	private:
		std::map<std::string, std::string> m_argsWithValues;
		std::set<std::string> m_args;
	};
}
