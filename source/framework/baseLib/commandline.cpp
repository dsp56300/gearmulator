#include"commandline.h"

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
					add(currentArg);

				currentArg = arg.substr(1);
			}
			else
			{
				if (!currentArg.empty())
				{
					add(currentArg, arg);
					currentArg.clear();
				}
				else
				{
					add(arg);
				}
			}
		}

		if (!currentArg.empty())
			add(currentArg);
	}
}