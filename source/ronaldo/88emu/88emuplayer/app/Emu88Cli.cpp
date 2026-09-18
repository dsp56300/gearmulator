#include <cerrno>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include "88emuplayer/app/Emu88LaunchOptions.h"
#include "88lib/hardwareDevice.h"
#include "88lib/rom/romloader.h"
#include "juce_audio_formats/juce_audio_formats.h"
#include "synthLib/plugin.h"
#include "synthLib/stereoPeakLimiter.h"
#if JUCE_WINDOWS
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#if JUCE_MAC
#include <sys/stdio.h>
#endif
#endif

namespace
{
    bool publishWave(const juce::TemporaryFile& temporary, const juce::File& output, const bool overwrite)
    {
        if (overwrite)
            return temporary.overwriteTargetFileWithTemporary();
#if JUCE_WINDOWS
        return MoveFileExW(temporary.getFile().getFullPathName().toWideCharPointer(),
                           output.getFullPathName().toWideCharPointer(), MOVEFILE_WRITE_THROUGH) != 0;
#else
        const auto source = temporary.getFile().getFullPathName();
        const auto destination = output.getFullPathName();
#if JUCE_MAC
        if (::renamex_np(source.toRawUTF8(), destination.toRawUTF8(), RENAME_EXCL) == 0)
            return true;
        if (errno != ENOTSUP && errno != EINVAL)
            return false;
#endif
        // Linking within the destination directory atomically refuses an existing name.
        return ::link(source.toRawUTF8(), destination.toRawUTF8()) == 0;
#endif
    }

    int render(const emu88Player::LaunchOptions& options)
    {
        using namespace emu88Player;
        int errorCode = 3;
        try
        {
            const auto configFile = launchFile(options.get("config", defaultDataFolder() + "config/88emuPlayer.xml"));
            if (options.has("config") && !configFile.existsAsFile())
                throw std::runtime_error("Config file not found: " + configFile.getFullPathName().toStdString());
            juce::PropertySet config;
            if (configFile.existsAsFile())
            {
                const auto xml = juce::parseXML(configFile);
                if (!xml || !xml->hasTagName("PROPERTIES"))
                    throw std::runtime_error("Invalid XML config file: " + configFile.getFullPathName().toStdString());
                for (const auto* value : xml->getChildWithTagNameIterator("VALUE"))
                {
                    const auto name = value->getStringAttribute("name");
                    if (name.isEmpty())
                        continue;
                    if (const auto* child = value->getFirstChildElement())
                        config.setValue(name, child);
                    else
                        config.setValue(name, value->getStringAttribute("val"));
                }
            }
            const auto romFolder = launchFile(options.get("rom-dir", defaultDataFolder() + "roms"));
            if (options.has("rom-dir") && !romFolder.isDirectory())
                throw std::runtime_error("ROM folder not found: " + romFolder.getFullPathName().toStdString());
            configureRomSearchPaths(options);
            if (options.has("list-devices"))
            {
                listDevices();
                return 0;
            }

            const auto savedModel = config.getIntValue("deviceModel", static_cast<int>(emu88Lib::DeviceModel::Sc88Pro));
            const auto model = options.has("device") ? parseDevice(options.get("device"))
                                                     : (emu88Lib::isDeviceModelValue(static_cast<uint32_t>(savedModel))
                                                            ? static_cast<emu88Lib::DeviceModel>(savedModel)
                                                            : emu88Lib::DeviceModel::Sc88Pro);
            const auto inventory = emu88Lib::RomLoader::scan();
            const auto romDevice = emu88Lib::RomLoader::toRomDevice(model);
            if (!inventory.isComplete(romDevice))
                throw std::runtime_error("Missing ROMs for " + std::string(deviceId(model)) + ":\n" +
                                         inventory.describeRequirements(romDevice));
            if (const auto warnings = inventory.warnings(romDevice); !warnings.empty())
                std::cerr << "ROM warning:\n" << warnings;
            if (options.has("pcm-card") && model != emu88Lib::DeviceModel::Cm32p &&
                model != emu88Lib::DeviceModel::Cm64)
                throw std::runtime_error(
                    "--pcm-card needs a device with a PCM card slot; use --device cm32p.");
            const auto pcmCard = loadPcmCard(options);
            if (!pcmCard.empty() && !options.has("quiet"))
                std::cerr << "PCM card: " << launchFile(options.get("pcm-card")).getFileName() << '\n';

            jucePlayer::MidiPlayer player;
            const auto loaded = player.addFiles(options.files);
            if (!loaded.errors.empty())
                throw std::runtime_error(loaded.errors.front());
            const auto reset = options.has("reset") ? parseReset(options.get("reset"))
                                                    : static_cast<jucePlayer::MidiPlayer::ResetMode>(
                                                          std::clamp(config.getIntValue("songResetMode", 2), 0, 3));
            player.setResetMode(reset);
            player.setPortCount(emu88Lib::getDeviceProfile(model).groupCount);
            player.setSongGapMs(static_cast<uint32_t>(
                options.number("song-gap-ms", std::clamp(config.getIntValue("songGapMs", 1000), 0, 60000))));
            const auto tailMs = options.number("tail-ms", 4000);
            player.setEndTailMs(static_cast<uint32_t>(tailMs));
            const auto savedAudio = config.getXmlValue("audioSetup");
            const auto sampleRate = options.number(
                "sample-rate", savedAudio ? savedAudio->getDoubleAttribute("audioDeviceRate", 44100) : 44100);
            if (!std::isfinite(sampleRate) || sampleRate < 8000 || sampleRate > 192000 ||
                std::floor(sampleRate) != sampleRate)
                throw std::runtime_error("Invalid output sample rate in config; override with --sample-rate.");
            const auto bits = static_cast<int>(options.number("bits", 24));
            const auto gain = options.number("gain", std::clamp(config.getDoubleValue("outputGain", 1), 0.0, 4.0));
            if (!std::isfinite(gain))
                throw std::runtime_error("Invalid output gain in config.");
            const auto outputFile = launchFile(options.get("output"));
            if (outputFile == launchFile(options.files.front()) || outputFile == configFile)
                throw std::runtime_error("Output must not replace the input or config file.");
            errorCode = 4;
            if (outputFile.exists() && !options.has("overwrite"))
                throw std::runtime_error("Output already exists; use --overwrite: " +
                                         outputFile.getFullPathName().toStdString());
            if (!outputFile.getParentDirectory().isDirectory())
                throw std::runtime_error("Output directory does not exist.");
            juce::TemporaryFile temporary(outputFile);
            auto stream = temporary.getFile().createOutputStream();
            if (!stream || !stream->openedOk())
                throw std::runtime_error("Cannot create output WAV.");
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                format.createWriterFor(stream.get(), sampleRate, 2, bits, {}, 0));
            if (!writer)
                throw std::runtime_error("Cannot create WAV writer.");
            stream.release(); // Writer owns it after successful construction.

            synthLib::DeviceCreateParams params;
            params.customData = static_cast<uint32_t>(model);
            // The player's startup settings unless overridden here, so a render starts from the board
            // state playback would.
            emu88Lib::BootOptions boot;
            boot.factoryReset = options.has("factory-reset")
                ? options.get("factory-reset") == "on"
                : config.getBoolValue("factoryResetOnLoad", boot.factoryReset);
            boot.fastBoot = options.has("fast-boot") ? options.get("fast-boot") == "on"
                                                     : config.getBoolValue("fastBoot", boot.fastBoot);
            emu88Lib::HardwareDevice device(params, boot, pcmCard);
            if (!device.isValid())
                throw std::runtime_error("Unable to create emulated device.");
            // Same setting as the player; it has to reach the device before the engine negotiates the rate.
            const auto analogMode = config.getIntValue("analogOutputMode", 0);
            device.setAnalogOutputMode(analogMode >= 0 &&
                                               emu88Lib::isAnalogOutputModeValue(static_cast<uint32_t>(analogMode))
                                           ? static_cast<emu88Lib::AnalogOutputMode>(analogMode)
                                           : emu88Lib::AnalogOutputMode::Off);
            synthLib::Plugin engine(&device, [](synthLib::Device*) { return nullptr; });
            engine.setMidiClockEnabled(false);
            engine.setResamplerMode(synthLib::Resampler::Mode::MameHq);
            engine.setHostSamplerate(static_cast<float>(sampleRate), 0);
            constexpr uint32_t blockSize = 256;
            engine.setBlockSize(blockSize);
            engine.setLatencyBlocks(0);
            juce::AudioBuffer<float> audio(2, blockSize);
            synthLib::TAudioOutputs outputs{};
            outputs[0] = audio.getWritePointer(0);
            outputs[1] = audio.getWritePointer(1);
            std::vector<synthLib::SMidiEvent> events, midiOut;
            auto process = [&](const uint32_t count, const bool playing)
            {
                events.clear();
                if (playing)
                    player.processBlock(events, count, sampleRate);
                for (const auto& event : events)
                    engine.addMidiEvent(event);
                engine.process({}, outputs, count, 120, 0, false, false);
                engine.getMidiOut(midiOut);
                midiOut.clear();
                if (engine.hasPendingDeviceSamplerate())
                    engine.applyPendingDeviceSamplerate();
            };
            if (!options.has("quiet"))
                std::cerr << "Booting " << deviceId(model) << "...\n";
            for (uint64_t remaining =
                     static_cast<uint64_t>(std::ceil(options.number("boot-ms", 5000) * sampleRate / 1000));
                 remaining;)
            {
                const auto count = static_cast<uint32_t>(std::min<uint64_t>(blockSize, remaining));
                process(count, false);
                remaining -= count;
            }
            // The player holds one settle after the reset and, for MT-32, a second one
            // after the arrangement. Derived rather than restated so the two cannot drift.
            constexpr auto settle = jucePlayer::MidiPlayer::kResetSettleMs / 1000.0;
            const auto preparation = reset == jucePlayer::MidiPlayer::ResetMode::Off ? 0.0
                : reset == jucePlayer::MidiPlayer::ResetMode::Mt32                   ? settle * 2
                                                                                     : settle;
            const auto duration = preparation + player.entries().front().durationSeconds + tailMs / 1000;
            if (!std::isfinite(duration) || duration < 0 || duration > 1.0e12)
                throw std::runtime_error("Invalid or excessive song duration.");
            const auto songFrames = static_cast<uint64_t>(std::ceil(duration * sampleRate)) + 1;
            const auto total = options.has("max-seconds")
                ? std::min<uint64_t>(songFrames,
                                     static_cast<uint64_t>(std::ceil(options.number("max-seconds", 0) * sampleRate)))
                : songFrames;
            // This writer produces RIFF, whose size fields are 32-bit.
            if (total > (0xffffffffull - 65536) / (2 * static_cast<uint64_t>(bits / 8)))
                throw std::runtime_error("Render exceeds RIFF WAV size limit; reduce --max-seconds or sample rate.");
            player.play(0);
            synthLib::StereoPeakLimiter limiter;
            limiter.prepare(sampleRate);
            const bool limitOutput =
                options.has("limiter") ? options.get("limiter") == "on" : config.getBoolValue("outputLimiter", false);
            uint64_t clipped = 0;
            int lastPercent = -1;
            for (uint64_t done = 0; done < total;)
            {
                const auto count = static_cast<uint32_t>(std::min<uint64_t>(blockSize, total - done));
                process(count, true);
                for (int channel = 0; channel < 2; ++channel)
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        auto& value = outputs[channel][i];
                        value *= static_cast<float>(gain);
                        if (!std::isfinite(value))
                            throw std::runtime_error("Non-finite audio sample.");
                    }
                limiter.process(outputs[0], outputs[1], count, limitOutput);
                for (int channel = 0; channel < 2; ++channel)
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        auto& value = outputs[channel][i];
                        if (std::abs(value) > 1)
                            ++clipped;
                        if (bits != 32)
                            value = std::clamp(value, -1.0f, 1.0f);
                    }
                if (!writer->writeFromAudioSampleBuffer(audio, 0, static_cast<int>(count)))
                    throw std::runtime_error("WAV write failed (disk full?).");
                done += count;
                const auto percent = static_cast<int>(done * 100 / total);
                if (!options.has("quiet") && percent / 10 != lastPercent / 10)
                    std::cerr << percent << "%\n";
                lastPercent = percent;
            }
            if (!writer->flush())
                throw std::runtime_error("WAV flush failed.");
            writer.reset();
            if (!publishWave(temporary, outputFile, options.has("overwrite")))
                throw std::runtime_error(
                    "Cannot publish completed WAV: destination exists or the filesystem refused publication.");
            if (clipped)
                std::cerr << "Warning: " << clipped
                          << (bits == 32 ? " samples exceed full scale (preserved in float WAV).\n"
                                         : " samples clipped; reduce --gain.\n");
            if (!options.has("quiet"))
                std::cerr << "Wrote " << total << " stereo frames to " << outputFile.getFullPathName() << '\n';
            return 0;
        }
        catch (const std::exception& error)
        {
            std::cerr << error.what() << '\n';
            return errorCode;
        }
    }
} // namespace

int main(int argc, char** argv)
{
    juce::StringArray args;
#if JUCE_WINDOWS
    int wideCount = 0;
    if (auto** wide = CommandLineToArgvW(GetCommandLineW(), &wideCount))
    {
        for (int i = 1; i < wideCount; ++i)
            args.add(juce::String(wide[i]));
        LocalFree(wide);
    }
#else
    for (int i = 1; i < argc; ++i)
        args.add(juce::String::fromUTF8(argv[i]));
#endif
    try
    {
        const auto options = emu88Player::LaunchOptions::parse(args, true);
        if (options.has("help"))
        {
            std::cout << emu88Player::LaunchOptions::help(true);
            return 0;
        }
        return render(options);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
