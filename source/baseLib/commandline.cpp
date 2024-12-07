#include"commandline.h"

#include <cmath>
#include <stdexcept>

namespace baseLib
{
	CommandLine::CommandLine(const int _argc, char* _argv[])
	{
		std::string currentArg;

		for (int i = 1; i < _argc; ++i)
		{
			std::string arg(_argv[i]);
			if (arg.empty())
				continue;

			if (arg[0] == '-')
			{
				if (!currentArg.empty())
					m_args.insert(currentArg);

				currentArg = arg.substr(1);
			}
			else
			{
				if (!currentArg.empty())
				{
					m_argsWithValues.insert(std::make_pair(currentArg, arg));
					currentArg.clear();
				}
				else
				{
					m_args.insert(arg);
				}
			}
		}

		if (!currentArg.empty())
			m_args.insert(currentArg);
	}

	std::string CommandLine::tryGet(const std::string& _key, const std::string& _value) const
	{
		const auto it = m_argsWithValues.find(_key);
		if (it == m_argsWithValues.end())
			return _value;
		return it->second;
	}

	std::string CommandLine::get(const std::string& _key, const std::string& _default/* = {}*/) const
	{
		const auto it = m_argsWithValues.find(_key);
		if (it == m_argsWithValues.end())
			return _default;
		return it->second;
	}

	float CommandLine::getFloat(const std::string& _key, const float _default/* = 0.0f*/) const
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

	int CommandLine::getInt(const std::string& _key, const int _default/* = 0*/) const
	{
		const std::string stringResult = get(_key);
		if (stringResult.empty())
			return _default;
		return atoi(stringResult.c_str());
	}

	bool CommandLine::contains(const std::string& _key) const
	{
		return m_args.find(_key) != m_args.end() || m_argsWithValues.find(_key) != m_argsWithValues.end();
	}
}