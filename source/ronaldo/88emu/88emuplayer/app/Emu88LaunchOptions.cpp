#include "88emuplayer/app/Emu88LaunchOptions.h"
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string_view>
#include "88lib/hardwareDevice.h"
#include "88lib/rom/romloader.h"
#include "baseLib/filesystem.h"
#include "synthLib/romLoader.h"

namespace emu88Player
{
    std::pair<int, int> parseOutputChannels(const std::string& value)
    {
        const auto comma = value.find(',');
        const auto parseIndex = [](const std::string_view text)
        {
            int index = 0;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), index);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || index < 1 || index > 1024)
                throw std::runtime_error("--output-channels requires two different channels between 1 and 1024.");
            return index - 1;
        };
        if (comma == std::string::npos)
            throw std::runtime_error("--output-channels requires two 1-based indices, e.g. 3,4.");
        const std::string_view text(value);
        const auto left = parseIndex(text.substr(0, comma));
        const auto right = parseIndex(text.substr(comma + 1));
        if (left == right)
            throw std::runtime_error("Choose two different output channels.");
        return {left, right};
    }

    std::string defaultDataFolder()
    {
        const auto* root = std::getenv("TUS_DATA_FOLDER");
        return baseLib::filesystem::validatePath(root && *root
                                                     ? root
                                                     : baseLib::filesystem::getSpecialFolderPath(
                                                           baseLib::filesystem::SpecialFolderType::UserDocuments)) +
            "The Usual Suspects/88emuPlayer/";
    }

    void configureRomSearchPaths(const LaunchOptions& options)
    {
        // Only one folder, searched recursively: --rom-dir, else the player's own data folder.
        // Every file of a ROM's size is hashed on each launch, and searching all of "The Usual
        // Suspects" hashed every other product's ROMs too - seconds before the window appeared.
        const auto folder = launchFile(options.has("rom-dir") ? options.get("rom-dir") : defaultDataFolder());
        synthLib::RomLoader::setSearchPath(folder.getFullPathName().toStdString());
    }

    const char* deviceId(const emu88Lib::DeviceModel model)
    {
        static constexpr const char* ids[] = {"sc88",   "sc88vl", "sc88pro", "sc8850",  "sc55mk2", "sc55",
                                              "sc55st", "cm300",  "scb55",   "rlp3237", "sc155",   "sc155mk2",
                                              "xpgs",   "sc8820", "cm32p",   "vegspro", "scc1a",   "cm64",
                                              "cm32l"};
        static_assert(std::size(ids) == emu88Lib::g_deviceMenuOrder.size(), "one ID per device model, in enum order");
        return ids[static_cast<size_t>(model)];
    }

    emu88Lib::DeviceModel parseDevice(const std::string& id)
    {
        for (const auto model : emu88Lib::g_deviceMenuOrder)
            if (id == deviceId(model))
                return model;
        throw std::runtime_error("Unknown device '" + id + "'; use --list-devices.");
    }

    jucePlayer::MidiPlayer::ResetMode parseReset(const std::string& name)
    {
        if (name == "off")
            return jucePlayer::MidiPlayer::ResetMode::Off;
        if (name == "gm")
            return jucePlayer::MidiPlayer::ResetMode::Gm;
        if (name == "gs")
            return jucePlayer::MidiPlayer::ResetMode::Gs;
        if (name == "mt32")
            return jucePlayer::MidiPlayer::ResetMode::Mt32;
        throw std::runtime_error("--reset must be off, gm, gs or mt32 (MT-32 tones on GS devices).");
    }

    juce::File launchFile(const std::string& path)
    {
        return juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(path.c_str()));
    }

    std::vector<uint8_t> loadPcmCard(const LaunchOptions& options)
    {
        if (!options.has("pcm-card"))
            return {};
        const auto file = launchFile(options.get("pcm-card"));
        juce::MemoryBlock data;
        if (!file.existsAsFile() || !file.loadFileAsData(data))
            throw std::runtime_error("PCM card not found: " + file.getFullPathName().toStdString());
        const auto* bytes = static_cast<const uint8_t*>(data.getData());
        std::vector<uint8_t> card(bytes, bytes + data.getSize());
        if (!emu88Lib::HardwareDevice::isPcmCardImage(card))
            throw std::runtime_error("Not a PCM card image (up to 512 KiB, with a tone list): " +
                                     file.getFullPathName().toStdString());
        return card;
    }

    std::string LaunchOptions::get(const std::string& key, const std::string& fallback) const
    {
        const auto found = values.find(key);
        return found == values.end() ? fallback : found->second;
    }

    double LaunchOptions::number(const std::string& key, const double fallback) const
    {
        return has(key) ? std::stod(get(key)) : fallback;
    }

    bool LaunchOptions::sessionOverrides() const
    {
        for (const auto& [key, value] : values)
            if (key != "config" && key != "save-settings")
                return true;
        return !files.empty() || !midiInputs.empty();
    }

    LaunchOptions LaunchOptions::parse(const juce::StringArray& args, const bool cli)
    {
        const std::set<std::string> common{"rom-dir",       "config",      "device",  "reset",
                                           "song-gap-ms",   "sample-rate", "gain",    "limiter",
                                           "factory-reset", "fast-boot",   "pcm-card"};
        const std::set<std::string> render{"output", "bits", "boot-ms", "tail-ms", "max-seconds"};
        const std::set<std::string> gui{"audio-backend",     "audio-device", "buffer-size",
                                        "midi-in",           "midi-out",     "virtual-ports",
                                        "virtual-port-name", "playlist",     "output-channels"};
        const std::set<std::string> flags = cli
            ? std::set<std::string>{"help", "list-devices", "overwrite", "quiet"}
            : std::set<std::string>{"help", "list-devices", "list-endpoints", "play", "save-settings"};
        LaunchOptions result;
        bool positional = false;
        for (int i = 0; i < args.size(); ++i)
        {
            const auto arg = args[i].toStdString();
            if (!positional && arg == "--")
            {
                positional = true;
                continue;
            }
            if (positional || arg.empty() || arg[0] != '-')
            {
                result.files.push_back(arg);
                continue;
            }
            if (arg.rfind("--", 0) != 0)
                throw std::runtime_error("Unknown option '" + arg + "'; use --help.");
            const auto equals = arg.find('=');
            const auto key = arg.substr(2, equals == std::string::npos ? equals : equals - 2);
            std::string value;
            if (flags.count(key))
            {
                if (equals != std::string::npos)
                    throw std::runtime_error("--" + key + " takes no value.");
                value = "true";
            }
            else
            {
                if (!common.count(key) && !(cli ? render : gui).count(key))
                    throw std::runtime_error("Unknown option '--" + key + "'; use --help.");
                if (equals != std::string::npos)
                    value = arg.substr(equals + 1);
                else if (i + 1 < args.size() && !args[i + 1].startsWith("--"))
                    value = args[++i].toStdString();
                if (value.empty())
                    throw std::runtime_error("--" + key + " requires a value.");
            }
            if (key == "playlist")
            {
                result.files.push_back(value);
                continue;
            }
            if (key == "midi-in")
            {
                result.midiInputs.push_back(value);
                continue;
            }
            if (!result.values.emplace(key, value).second)
                throw std::runtime_error("Duplicate option --" + key);
        }
        if (result.has("device"))
            (void)parseDevice(result.get("device"));
        if (result.has("output-channels"))
            (void)parseOutputChannels(result.get("output-channels"));
        if (result.has("reset"))
            (void)parseReset(result.get("reset"));
        for (const auto& [key, range] :
             std::map<std::string, std::pair<double, double>>{{"sample-rate", {8000, 192000}},
                                                              {"buffer-size", {16, 32768}},
                                                              {"gain", {0, 4}},
                                                              {"song-gap-ms", {0, 60000}},
                                                              {"boot-ms", {0, 60000}},
                                                              {"tail-ms", {0, 600000}},
                                                              {"max-seconds", {0.001, 86400}},
                                                              {"bits", {16, 32}}})
        {
            if (!result.has(key))
                continue;
            const auto value = result.get(key);
            size_t end = 0;
            double n = 0;
            try
            {
                n = std::stod(value, &end);
            }
            catch (...)
            {
                end = 0;
            }
            if (end != value.size() || !std::isfinite(n) || n < range.first || n > range.second ||
                (key != "gain" && key != "max-seconds" && std::floor(n) != n))
                throw std::runtime_error("Invalid value for --" + key + ": " + value);
        }
        if (result.has("bits") && result.get("bits") != "16" && result.get("bits") != "24" &&
            result.get("bits") != "32")
            throw std::runtime_error("--bits must be 16, 24 or 32 (16/24-bit integer PCM or 32-bit float).");
        for (const auto* key : {"virtual-ports", "limiter", "factory-reset", "fast-boot"})
            if (result.has(key) && result.get(key) != "on" && result.get(key) != "off")
                throw std::runtime_error(std::string("--") + key + " must be on or off.");
        if (cli && !result.has("help") && !result.has("list-devices") &&
            (result.files.size() != 1 || !result.has("output")))
            throw std::runtime_error("Supply one MIDI/RCP input and --output WAV; use --help.");
        return result;
    }

    std::string LaunchOptions::help(const bool cli)
    {
        return std::string(cli ? "88EmuCli [options] INPUT --output OUTPUT.wav\n"
                               : "88emuPlayer [options] [MIDI/RCP files...]\n") +
            "  --help                 Show this help\n"
            "  --list-devices         List stable device IDs and ROM availability\n"
            "  --rom-dir PATH         Use only this ROM folder (recursive)\n"
            "  --config PATH          Read this settings file\n"
            "  --device ID            Initial emulated device (default sc88pro)\n"
            "  --reset off|gm|gs|mt32  Before each song; mt32 selects MT-32 tones on GS\n"
            "  --song-gap-ms N        Automatic song gap, 0..60000 milliseconds (default 1000)\n"
            "  --sample-rate HZ       Output sample rate, 8000..192000\n"
            "  --limiter on|off       Output peak limiter (default off)\n"
            "  --factory-reset on|off Factory reset when the device loads (default on)\n"
            "  --fast-boot on|off     Boot 10 s longer to start past the intro (default off)\n"
            "  --pcm-card PATH        CM-32P: PCM card image in the card slot (SN-U110 series)\n"
            "  --gain N               Linear output gain (CLI 0..4, GUI 0..2)\n" +
            (cli ? "  --output PATH          Stereo WAV output; existing files are protected\n"
                   "  --bits 16|24|32        16/24-bit PCM or 32-bit float (default 24)\n"
                   "  --boot-ms N            Discard boot audio before playback (default 5000)\n"
                   "  --tail-ms N            Release tail after final MIDI event (default 4000)\n"
                   "  --max-seconds N        Truncate output to this duration\n"
                   "  --overwrite            Replace an existing output after successful rendering\n"
                   "  --quiet                Suppress progress\n"
                   "Exit codes: 0 success, 2 arguments, 3 input/ROM/config, 4 output/render failure.\n"
                 : "  --playlist PATH        Add a file, repeatable; positional files work too\n"
                   "  --play                 Start the preloaded playlist\n"
                   "  --audio-backend NAME   Exact backend name from --list-endpoints\n"
                   "  --audio-device NAME    Exact output name, 'none', or 'system'\n"
                   "  --buffer-size N        Audio callback frames\n"
                   "  --output-channels L,R  Two distinct 1-based hardware output channels\n"
                   "  --midi-in ID_OR_NAME   Repeatable; 'none' disables saved inputs\n"
                   "  --midi-out ID_OR_NAME  Output endpoint, or 'none'\n"
                   "  --virtual-ports on|off Enable or disable virtual MIDI ports\n"
                   "  --virtual-port-name N Prefix for virtual MIDI IN/OUT A-D names\n"
                   "  --list-endpoints      List audio backends/devices and MIDI IDs\n"
                   "  --save-settings       Allow this launch's overrides to persist\n"
                   "Overrides are session-local by default. Preloading does not start playback.\n") +
            "Use -- before filenames beginning with '-'. Quote paths containing spaces.\n";
    }

    void listDevices()
    {
        for (const auto model : emu88Lib::g_deviceMenuOrder)
            if (emu88Lib::isDeviceListed(model))
                std::cout << deviceId(model) << "\t" << emu88Lib::getDeviceProfile(model).displayName << "\t"
                          << (emu88Lib::RomLoader::isDeviceAvailable(model) ? "available" : "missing ROMs") << '\n';
    }
} // namespace emu88Player
