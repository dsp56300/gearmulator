#include "88emuplayer/app/Emu88LaunchOptions.h"
#include <iostream>
#include <stdexcept>

namespace
{
	void check(bool value, const char* message) { if(!value) throw std::runtime_error(message); }
	void rejects(juce::StringArray args, bool cli = true)
	{
		try { (void)emu88Player::LaunchOptions::parse(args, cli); }
		catch(const std::runtime_error&) { return; }
		throw std::runtime_error("Invalid arguments accepted: " + args.joinIntoString(" ").toStdString());
	}
}

int main()
{
	try
	{
		using namespace emu88Player;
		const auto cli = LaunchOptions::parse({"--rom-dir", juce::String::fromUTF8("/ROM \xe9\x9f\xb3\xe6\xa5\xbd"), "--device=sc55mk2", "--reset", "mt32", "--sample-rate=48000", "--bits", "24", "--output", "out song.wav", "--", juce::String::fromUTF8("-\xe6\x9b\xb2.r36")}, true);
		check(cli.files == std::vector<std::string>{"-\xe6\x9b\xb2.r36"}, "Unicode positional path");
		check(cli.get("rom-dir") == "/ROM \xe9\x9f\xb3\xe6\xa5\xbd", "Unicode ROM path");
		check(cli.number("sample-rate", 0) == 48000, "equals syntax");
		const auto gui = LaunchOptions::parse({"first.mid", "--playlist", juce::String::fromUTF8("\xe6\x9b\xb2.rcp"), "last.midi", "--midi-in", "one", "--midi-in=two", "--audio-device", "Loopback 1", "--virtual-port-name", juce::String::fromUTF8("Game \xe9\x9f\xb3\xe6\xa5\xbd"), "--song-gap-ms", "125", "--play"}, false);
		check(gui.files == std::vector<std::string>{"first.mid", "\xe6\x9b\xb2.rcp", "last.midi"}, "Playlist ordering");
		check(gui.midiInputs == std::vector<std::string>{"one", "two"}, "Repeat MIDI inputs");
		check(gui.sessionOverrides(), "Session overrides");
		check(parseOutputChannels("4,3") == std::pair<int, int>{3, 2}, "Reversed output channels");
		check(parseOutputChannels("1,1024") == std::pair<int, int>{0, 1023}, "Output channel bounds");
		for(const auto* bad : {"4294967297,2", "999999999999999999999,2", "1,1025", "0,2", "1,1", "1,", ",2", "1,2,3", "-1,2"})
			rejects({"--help", "--output-channels", bad}, false);
		check(!LaunchOptions::parse({"--config", "custom.xml"}, false).sessionOverrides(), "Config selection alone");
		for(const auto model : emu88Lib::g_deviceMenuOrder) check(parseDevice(deviceId(model)) == model, "Device ID roundtrip");
		check(parseDevice("vegspro") == emu88Lib::DeviceModel::VeGsPro, "VE-GS Pro device ID");
		check(parseDevice("scc1a") == emu88Lib::DeviceModel::Scc1a, "SCC-1A device ID");
		for(const auto* reset : {"off", "gm", "gs", "mt32"}) (void)parseReset(reset);
		for(const auto* bad : {"NaN", "inf", "-1", "1x", "1.5", "60001", "1e1000"}) rejects({"--help", "--song-gap-ms", bad});
		for(const auto* bad : {"0", "8000.5", "192001", "44100Hz"}) rejects({"--help", "--sample-rate", bad});
		rejects({"--reset=GS", "--help"});
		rejects({"--device=unknown-device", "--help"});
		rejects({"--gain", "nan", "--help"});
		rejects({"--bits=17", "--help"});
		rejects({"--output"});
		rejects({"--output", "a.wav", "a.mid", "b.mid"});
		rejects({"--device=sc88", "--device=sc55", "--help"});
		rejects({"--overwrite=false", "--help"});
		rejects({"--audio-device=none", "--help"});
		for(const bool isCli : {false, true})
			for(const std::string option : {"limiter", "factory-reset", "fast-boot"})
			{
				rejects({("--" + option + "=yes").c_str(), "--help"}, isCli);
				for(const auto* mode : {"on", "off"})
					check(LaunchOptions::parse({("--" + option).c_str(), mode, "--help"}, isCli).get(option) == mode, "On/off option");
			}
		check(LaunchOptions::parse({"--fast-boot", "on"}, false).sessionOverrides(), "Startup options are session overrides");
		for(const bool isCli : {false, true})
		{
			check(LaunchOptions::parse({"--pcm-card", "card.bin", "--help"}, isCli).get("pcm-card") == "card.bin", "PCM card option");
		}
		bool missingCardRejected = false;
		try { (void)loadPcmCard(LaunchOptions::parse({"--pcm-card", "/nonexistent/card.bin", "--help"}, true)); }
		catch(const std::runtime_error&) { missingCardRejected = true; }
		check(missingCardRejected, "Missing PCM card rejected");
		rejects({"--virtual-ports=yes"}, false);
		rejects({"--output=a.wav"}, false);
		rejects({"--device", "--play"}, false);
		check(LaunchOptions::parse({"--help"}, true).has("help"), "Help without input");
		check(LaunchOptions::parse({"--list-devices"}, true).has("list-devices"), "Discovery without input");
		std::cout << "Launch option validation passed\n";
		return 0;
	}
	catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
