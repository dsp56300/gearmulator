#pragma once

#include <string>

namespace pluginManager
{
	// A line in a file beside the settings, for the questions a report from somebody else cannot
	// answer on its own: which server answered, what was downloaded, where it went, and what the
	// matter was when it did not.
	//
	// Off until a program turns it on. The library is also what the tests run, and they have no
	// business leaving a file behind on a machine that only ran them.
	class Log
	{
	public:
		// log.txt beside settings.xml, trimmed when it grows past a few hundred kilobytes - what is
		// worth keeping is the last run or two, and a file nobody ever empties grows for years.
		static void start();
		static void stop();

		static void write(const std::string& _line);
	};
}
