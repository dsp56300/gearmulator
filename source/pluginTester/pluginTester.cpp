#include <chrono>

#include "fakeAudioDevice.h"
#include "pluginHost.h"
#include "logger.h"
#include "baseLib/commandline.h"
#include "baseLib/filesystem.h"

class JuceAppLifetimeObjects
{
public:
	JuceAppLifetimeObjects()
	{
		MessageManager::getInstance();
	}
	~JuceAppLifetimeObjects()
	{
        DeletedAtShutdown::deleteAll();
		MessageManager::deleteInstance();
	}
private:
	JUCE_DECLARE_NON_COPYABLE(JuceAppLifetimeObjects)
	JUCE_DECLARE_NON_MOVEABLE(JuceAppLifetimeObjects)
};

int main(const int _argc, char* _argv[])
{
	baseLib::CommandLine cmdLine(_argc, _argv);

	StdoutLogger logger;

	auto error = [](const String& _msg) -> int
	{
		Logger::writeToLog("Error: " + _msg);
		Logger::writeToLog("Usage:\n"
			"pluginTester -plugin <pathToPlugin> [-seconds n -blocks n -blocksize n -samplerate x -forever]");
		return 1;
	};
	try
	{
	    ConsoleApplication app;

		std::string pluginPathName = cmdLine.get("plugin");

		if (pluginPathName.empty())
		{
			return error("No plugin specified");
		}

	    {
		    // juce wants the folder for a VST3/LV2 plugin instead of the actual file
		    const auto lowercase = baseLib::filesystem::lowercase(pluginPathName);

		    auto start = lowercase.find(".vst3");
		    if (start == std::string::npos)
				start = lowercase.find(".lv2");
		    if (start == std::string::npos)
			    start = lowercase.find(".component");
		    if (start == std::string::npos)
			    start = lowercase.find(".vst");

		    if (start != std::string::npos)
		    {
			    auto slash = pluginPathName.find_first_of("\\/", start);

			    if (slash != std::string::npos)
				    pluginPathName = pluginPathName.substr(0, slash);
		    }
	    }

	    JuceAppLifetimeObjects jalto;

	    CommandLinePluginHost pluginHost;

		const auto& formatManager = pluginHost.getFormatManager();

		PluginDescription desc;

		for (int i = 0; i < formatManager.getNumFormats(); ++i)
		{
			auto* format = formatManager.getFormat(i);

			if (!format)
				continue;

			Logger::writeToLog("Attempt to load plugin as type " + format->getName());

		    KnownPluginList plugins;

			OwnedArray<PluginDescription> typesFound;
			plugins.scanAndAddFile(pluginPathName, true,typesFound, *format);

			const auto types = plugins.getTypes();

			if (types.isEmpty())
				continue;

			desc = types.getFirst();
			break;
		}

		if (desc.fileOrIdentifier.isEmpty())
			return error("Failed to find plugin " + pluginPathName);

	    if (!pluginHost.loadPlugin(desc))
			return error("Failed to load plugin " + pluginPathName);

		FakeAudioIODevice audioDevice;

		const uint32_t numIns = pluginHost.getCurrentProcessor()->getTotalNumInputChannels();
		const uint32_t numOuts = pluginHost.getCurrentProcessor()->getTotalNumOutputChannels();

		const auto blocksize = cmdLine.getInt("blocksize", 512);
		const auto samplerate = cmdLine.getFloat("samplerate", 48000.0f);

		auto res = audioDevice.open(numIns, numOuts, samplerate, blocksize);

		if (res.isNotEmpty())
			return error("Failed to open audio device: " + res);

		audioDevice.start(&pluginHost);

		const auto forever = cmdLine.contains("forever");

		if (forever)
		{
			uint64_t blockCount = 0;
			uint64_t sr = static_cast<uint64_t>(samplerate);

			uint64_t lastMinutes = 0;

			using Clock = std::chrono::high_resolution_clock;

			const auto tBegin = Clock::now();

			while (true)
			{
				audioDevice.processAudio();
				++blockCount;

				auto totalSeconds = blockCount * blocksize / sr;
				auto minutes = totalSeconds / 60;
				auto hours = minutes / 60;
				auto seconds = totalSeconds - minutes * 60;
				minutes -= hours * 60;

				if (minutes != lastMinutes)
				{
					const auto t2 = Clock::now();
					const auto duration = std::chrono::duration_cast<std::chrono::seconds>(t2 - tBegin).count();

					const auto speed = static_cast<double>(totalSeconds) * 100.0 / static_cast<double>(duration);

					char temp[64];
					(void)snprintf(temp, sizeof(temp), "Executed %02uh %02um %02us, Speed %2.2f%%", static_cast<uint32_t>(hours), static_cast<uint32_t>(minutes), static_cast<uint32_t>(seconds), speed);
					Logger::writeToLog(temp);
					lastMinutes = minutes;
				}
			}
		}

		const auto seconds = cmdLine.getInt("seconds", 0);
		auto blocks = cmdLine.getInt("blocks", 0);

		if (blocks && seconds)
			return error("Cannot specify both blocks and seconds");

		if (seconds)
		{
			blocks = static_cast<int>(samplerate) / blocksize * seconds;
			if (blocks == 0)
				blocks = 1;
		}

		int lastPercent = -1;

		char temp[64];

		for (int i=0; i<blocks; ++i)
		{
			audioDevice.processAudio();

			const auto percent = i * 100 / blocks;

			if (percent == lastPercent)
				continue;
			lastPercent = percent;

			(void)snprintf(temp, sizeof(temp), "Progress: %d%% (%d/%d blocks)", percent, i, blocks);
			Logger::writeToLog(temp);
		}

		(void)snprintf(temp, sizeof(temp), "Progress: %d%% (%d/%d blocks)", 100, blocks, blocks);
		Logger::writeToLog(temp);

	    return 0;
	}
	catch (const std::exception& e)
	{
		juce::Logger::writeToLog(e.what());
		return 1;
	}
}
