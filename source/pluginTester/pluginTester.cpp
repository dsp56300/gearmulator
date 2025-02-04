#include "fakeAudioDevice.h"
#include "pluginHost.h"
#include "logger.h"
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
	StdoutLogger logger;

	try
	{
	    if (_argc < 2)
	    {
	        Logger::writeToLog("Usage: pluginTester <path_to_vst2/3_plugin>");
	        return 1;
	    }

	    ConsoleApplication app;

		const String pluginPathName = _argv[1];
		const String pluginPath = baseLib::filesystem::getPath(_argv[1]);
		const String pluginFilename = baseLib::filesystem::getFilenameWithoutPath(_argv[1]);

		JuceAppLifetimeObjects jalto;

	    CommandLinePluginHost pluginHost;

		const auto& formatManager = pluginHost.getFormatManager();

		PluginDescription desc;

		for (int i = 0; i < formatManager.getNumFormats(); ++i)
		{
			auto* format = formatManager.getFormat(i);

			if (!format)
				continue;

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
		{
			Logger::writeToLog("Failed to find plugin " + pluginPathName);
			return 3;
		}

	    if (!pluginHost.loadPlugin(desc))
	    {
	        Logger::writeToLog("Failed to load plugin " + pluginPathName);
	        return 1;
	    }

		FakeAudioIODevice audioDevice;

		const uint32_t numIns = pluginHost.getCurrentProcessor()->getTotalNumInputChannels();
		const uint32_t numOuts = pluginHost.getCurrentProcessor()->getTotalNumOutputChannels();

		auto res = audioDevice.open(numIns, numOuts, 48000, 512);

		if (res.isNotEmpty())
		{
			Logger::writeToLog("Failed to open audio device: " + res);
			return 2;
		}

		audioDevice.start(&pluginHost);

		while (true)
		{
			audioDevice.processAudio();
		}

	    return 0;
	}
	catch (const std::exception& e)
	{
		juce::Logger::writeToLog(e.what());
		return 1;
	}
}
