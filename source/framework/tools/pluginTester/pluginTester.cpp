#include <algorithm>
#include <cctype>
#include <chrono>

#include "fakeAudioDevice.h"
#include "pluginHost.h"
#include "logger.h"
#include "baseLib/binarystream.h"
#include "baseLib/commandline.h"
#include "baseLib/filesystem.h"
#include "baseLib/os.h"

namespace
{
	// The host wraps the plugin state per format: VST2 keeps the processor state as it is, the VST3 host and the LV2
	// plugin store it as MemoryBlock::toBase64Encoding text ("<size>.<chars>") in XML or Turtle. Find it by the
	// length-prefixed string "DSP56300" that jucePluginLib::Processor writes first
	std::vector<uint8_t> findProcessorState(const MemoryBlock& _hostState)
	{
		static constexpr uint8_t magic[] = {8, 0, 0, 0, 'D', 'S', 'P', '5', '6', '3', '0', '0'};

		auto find = [](const void* _data, const size_t _size) -> std::vector<uint8_t>
		{
			const auto* begin = static_cast<const uint8_t*>(_data);
			const auto* end = begin + _size;
			const auto* it = std::search(begin, end, std::begin(magic), std::end(magic));
			return {it, end};
		};

		if (auto s = find(_hostState.getData(), _hostState.getSize()); !s.empty())
			return s;

		const auto* data = static_cast<const uint8_t*>(_hostState.getData());
		const auto size = _hostState.getSize();

		auto isBase64 = [](const uint8_t _c) { return std::isalnum(_c) || _c == '.' || _c == '+'; };

		for (size_t i = 0; i < size; ++i)
		{
			if (!std::isdigit(data[i]) || (i > 0 && isBase64(data[i - 1])))
				continue;

			auto end = i;
			while (end < size && isBase64(data[end]))
				++end;

			MemoryBlock decoded;
			if (decoded.fromBase64Encoding(String(reinterpret_cast<const char*>(data + i), end - i)))
			{
				if (auto s = find(decoded.getData(), decoded.getSize()); !s.empty())
					return s;
			}
			i = end;
		}
		return {};
	}

	// size of the device state in the MIDI chunk of a processor state, without the version and type byte that
	// synthLib::Plugin::getState puts in front. 0 if there is none, as with the DummyDevice of a plugin without ROM
	size_t getDeviceStateSize(const std::vector<uint8_t>& _processorState)
	{
		baseLib::BinaryStream s(_processorState);
		s.readString();			// magic
		s.read<uint32_t>();		// version

		std::vector<uint8_t> chunks;
		s.read(chunks);

		baseLib::BinaryStream cs(chunks);
		baseLib::ChunkReader cr(cs);

		size_t size = 0;
		cr.add("MIDI", 1, [&](baseLib::BinaryStream& _s, uint32_t)
		{
			std::vector<uint8_t> deviceState;
			_s.read(deviceState);
			size = deviceState.size() > 2 ? deviceState.size() - 2 : 0;
		});
		cr.read();
		return size;
	}
}

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
	baseLib::disableErrorDialogs();

	baseLib::CommandLine cmdLine(_argc, _argv);

	StdoutLogger logger;

	auto error = [](const String& _msg) -> int
	{
		Logger::writeToLog("Error: " + _msg);
		Logger::writeToLog("Usage:\n"
			"pluginTester -plugin <pathToPlugin> [-seconds n -blocks n -blocksize n -samplerate x -forever -repeat n]\n"
			"             [-loadstate <file> -dumpstate <file> -editor]\n"
			"\n"
			"-loadstate  loads a state written by -dumpstate before the blocks are processed\n"
			"-dumpstate  writes the plugin state after the blocks, the last repeat wins. Fails if it holds no device\n"
			"            state, which is the case without a ROM or before the firmware booted. The host wraps the\n"
			"            state per plugin format, compare dumps of one format only\n"
			"-editor     creates the editor before the blocks and deletes it after them in every repeat, without a\n"
			"            window\n"
			"\n"
			"Before using -dumpstate to prove that a change keeps the state the same:\n"
			"- install the ROMs, use the same config for the plugin before and after (resampler, skin variables)\n"
			"  and a build that is not a demo build\n"
			"- find a block count past the firmware boot of the product: a dump after n blocks must equal one after\n"
			"  2n blocks, and two runs must agree");
		return 1;
	};

	const auto repeatCount = cmdLine.getInt("repeat", 1);
	try
	{
	  for (int repeatIdx = 0; repeatIdx < repeatCount; ++repeatIdx)
	  {
		if (repeatCount > 1)
		{
			char msg[64];
			(void)snprintf(msg, sizeof(msg), "=== Repeat %d / %d ===", repeatIdx + 1, repeatCount);
			Logger::writeToLog(msg);
		}

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

		auto& processor = *pluginHost.getCurrentProcessor();

		if (const auto loadState = cmdLine.get("loadstate"); !loadState.empty())
		{
			std::vector<uint8_t> state;
			if (!baseLib::filesystem::readFile(state, loadState) || state.empty())
				return error("Failed to read state from " + loadState);
			processor.setStateInformation(state.data(), static_cast<int>(state.size()));
			Logger::writeToLog("Loaded state from " + loadState);
		}

		// declared after the host, so it is deleted before the plugin: a processor must not be deleted while its editor exists
		std::unique_ptr<AudioProcessorEditor> editor;

		if (cmdLine.contains("editor"))
		{
			editor.reset(processor.createEditorIfNeeded());
			if (!editor)
				return error("The plugin did not create an editor");
			Logger::writeToLog("Created editor");
		}

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

				auto formatDuration = [](const uint64_t _seconds) -> std::string
				{
					char temp[64];
					const auto minutes = _seconds / 60;
					const auto hours = minutes / 60;
					const auto s = _seconds - minutes * 60;
					const auto m = minutes - hours * 60;
					(void)snprintf(temp, sizeof(temp), "%02uh %02um %02us", static_cast<uint32_t>(hours), static_cast<uint32_t>(m), static_cast<uint32_t>(s));
					return temp;
				};

				const auto totalSeconds = blockCount * blocksize / sr;
				const auto minutes = totalSeconds / 60;

				if (minutes != lastMinutes)
				{
					const auto t2 = Clock::now();
					const auto duration = std::chrono::duration_cast<std::chrono::seconds>(t2 - tBegin).count();

					const auto speed = static_cast<double>(totalSeconds) * 100.0 / static_cast<double>(duration);

					char temp[64];
					(void)snprintf(temp, sizeof(temp), "Processed %s, elapsed %s, speed %2.2f%%", formatDuration(totalSeconds).c_str(), formatDuration(duration).c_str(), speed);
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

		if (const auto dumpState = cmdLine.get("dumpstate"); !dumpState.empty())
		{
			MemoryBlock state;
			processor.getStateInformation(state);

			const auto processorState = findProcessorState(state);
			if (processorState.empty())
				return error("The plugin state holds no state of a gearmulator plugin");

			// without a device state, two dumps would compare equal for the wrong reason
			const auto deviceStateSize = getDeviceStateSize(processorState);
			if (!deviceStateSize)
				return error("The plugin state holds no device state: no ROM, or the firmware did not finish booting yet (process more blocks)");

			if (!baseLib::filesystem::writeFile(dumpState, static_cast<const uint8_t*>(state.getData()), state.getSize()))
				return error("Failed to write state to " + dumpState);

			Logger::writeToLog("Wrote state to " + String(dumpState) + ", " + String(state.getSize()) + " bytes, device state " + String(deviceStateSize) + " bytes");
		}

	  } // end repeat loop
	    return 0;
	}
	catch (const std::exception& e)
	{
		juce::Logger::writeToLog(e.what());
		return 1;
	}
}
