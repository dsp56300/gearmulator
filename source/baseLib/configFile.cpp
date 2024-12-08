#include "configFile.h"

#include <fstream>

namespace baseLib
{
	namespace
	{
		bool needsTrim(const char _c)
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

		std::string trim(std::string _s)
		{
			while (!_s.empty() && needsTrim(_s.front()))
				_s = _s.substr(1);
			while (!_s.empty() && needsTrim(_s.back()))
				_s = _s.substr(0, _s.size() - 1);
			return _s;
		}
	}

	ConfigFile::ConfigFile(const char* _filename)
	{
		std::ifstream file(_filename, std::ios::in);

		if (!file.is_open())
			return;

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

			add(key, val);
		}
	}

	ConfigFile::ConfigFile(const std::string& _filename) : ConfigFile(_filename.c_str())
	{
	}
}
