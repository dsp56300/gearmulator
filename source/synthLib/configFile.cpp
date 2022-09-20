#include "configFile.h"

#include <fstream>

#include "dsp56kEmu/logging.h"

namespace synthLib
{
	static bool needsTrim(char _c)
	{
		switch (_c)
		{
		case ' ':
		case '\r':
		case '\n':
		case '\t':
			return true;
		default:
			return false;
		}
	}

	static std::string trim(std::string _s)
	{
		while (!_s.empty() && needsTrim(_s.front()))
			_s = _s.substr(1);
		while (!_s.empty() && needsTrim(_s.back()))
			_s = _s.substr(0, _s.size() - 1);
		return _s;
	}

	ConfigFile::ConfigFile(const char* _filename)
	{
		std::ifstream file(_filename, std::ios::in);

		if (!file.is_open())
			throw std::runtime_error("Failed to open config file " + std::string(_filename));

		std::string line;

		while(std::getline(file, line))
		{
			if(line.empty())
				continue;

			// // comment?
			if (line.front() == '#' || line.front() == ';')
				continue;

			const auto posEq = line.find('=');

			if (posEq == std::string::npos)
				continue;

			const auto key = trim(line.substr(0, posEq));
			const auto val = trim(line.substr(posEq + 1));

			m_values.emplace_back(std::make_pair(key, val));
		}
	}
}