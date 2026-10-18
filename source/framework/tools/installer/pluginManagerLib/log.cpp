#include "log.h"

#include "config.h"
#include "platformUtils.h"

#include <memory>
#include <mutex>

#include "juce_core/juce_core.h"

namespace pluginManager
{
	namespace
	{
		std::mutex g_mutex;

		// Written to from an install worker as well as from the message thread. juce::FileLogger
		// guards the writing itself; the mutex here is for making it in the first place.
		std::unique_ptr<juce::FileLogger> g_logger;
		bool g_wanted = false;
	}

	void Log::start()
	{
		const std::lock_guard<std::mutex> lock(g_mutex);

		g_wanted = true;
	}

	void Log::stop()
	{
		const std::lock_guard<std::mutex> lock(g_mutex);

		g_wanted = false;
		g_logger.reset();
	}

	void Log::write(const std::string& _line)
	{
		const std::lock_guard<std::mutex> lock(g_mutex);

		if (!g_wanted)
			return;

		// Made on the first line there is to write, not when the program starts. Most runs of the
		// headless program have nothing to say, and a file of nothing but "log started" is worse
		// than no file: it buries the runs that did.
		if (!g_logger)
		{
			const auto file = juce::File(PlatformUtils::getSettingsPath("log.txt"));

			g_logger = std::make_unique<juce::FileLogger>(file,
				juce::String(g_appName) + " " + g_appVersion, 256 * 1024);
		}

		g_logger->logMessage(juce::Time::getCurrentTime().toISO8601(true) + "  " + juce::String(_line));
	}
}
