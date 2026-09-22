#include "88emuplayer/app/Emu88VideoRender.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "88emuplayer/Emu88Processor.h"
#include "88emuplayer/ui/Emu88Editor.h"
#include "88emuplayer/ui/Emu88EditorBindings.h"
#include "88lib/rom/romloader.h"
#include "juceRmlUi/juceRmlComponent.h"
#include "juce_audio_formats/juce_audio_formats.h"

#if JUCE_WINDOWS
#include <io.h>
#else
#include <csignal>
#endif

namespace emu88Player
{
    using editor::g_defaultHeight;
    using editor::g_defaultWidth;

    namespace
    {
        // The editor is a fixed aspect ratio, so the render size follows from a scale factor alone.
        // h264 in yuv420p halves both axes, so both have to stay even.
        struct FrameSize
        {
            int width = 0;
            int height = 0;
        };

        FrameSize renderSizeFor(const int _scalePercent)
        {
            const auto even = [](const int _value) { return _value & ~1; };
            return {std::max(2, even(g_defaultWidth * _scalePercent / 100)),
                    std::max(2, even(g_defaultHeight * _scalePercent / 100))};
        }

        FrameSize parseVideoSize(const std::string& _text)
        {
            const auto x = _text.find_first_of("xX");
            if (x == std::string::npos)
                throw std::runtime_error("--video-size takes WIDTHxHEIGHT, e.g. 1920x1080.");
            try
            {
                size_t end = 0;
                const auto width = std::stoi(_text.substr(0, x), &end);
                if (end != x)
                    throw std::invalid_argument("width");
                const auto rest = _text.substr(x + 1);
                const auto height = std::stoi(rest, &end);
                if (end != rest.size())
                    throw std::invalid_argument("height");
                if (width < 16 || height < 16 || width > 7680 || height > 4320 || (width & 1) || (height & 1))
                    throw std::invalid_argument("range");
                return {width, height};
            }
            catch (const std::exception&)
            {
                throw std::runtime_error("--video-size takes even WIDTHxHEIGHT between 16x16 and 7680x4320.");
            }
        }

        // The command goes through a shell, so every path has to survive it intact.
        std::string shellQuote(const std::string& _argument)
        {
#if JUCE_WINDOWS
            // cmd.exe has no escape for a quote inside a quoted argument, so a path holding one
            // cannot be passed on safely. % would be expanded before ffmpeg ever sees it.
            if (_argument.find('"') != std::string::npos || _argument.find('%') != std::string::npos)
                throw std::runtime_error("Paths containing '\"' or '%' cannot be passed to ffmpeg: " + _argument);
            return '"' + _argument + '"';
#else
            std::string quoted = "'";
            for (const auto c : _argument)
            {
                if (c == '\'')
                    quoted += "'\\''";
                else
                    quoted += c;
            }
            return quoted + '\'';
#endif
        }

        void requireFfmpeg(const std::string& _ffmpeg)
        {
            juce::ChildProcess probe;
            if (!probe.start(juce::StringArray(juce::String::fromUTF8(_ffmpeg.c_str()), "-version")) ||
                !probe.waitForProcessToFinish(20000) || probe.getExitCode() != 0)
                throw std::runtime_error("Cannot run ffmpeg (" + _ffmpeg +
                                         "). Install it or point --ffmpeg at the executable.");
        }

        // ffmpeg's stdin takes the frames; everything else it needs is on its command line. juce's
        // ChildProcess only reads a child's output, so this one is opened as a pipe.
        class FfmpegPipe
        {
        public:
            FfmpegPipe(const std::string& _commandLine)
            {
#if !JUCE_WINDOWS
                // A dead encoder must surface as a write error, not as this process being killed.
                std::signal(SIGPIPE, SIG_IGN);
#endif
#if JUCE_WINDOWS
                m_pipe = _popen(_commandLine.c_str(), "wb");
#else
                m_pipe = popen(_commandLine.c_str(), "w");
#endif
                if (!m_pipe)
                    throw std::runtime_error("Cannot start ffmpeg.");
            }

            FfmpegPipe(const FfmpegPipe&) = delete;
            FfmpegPipe& operator=(const FfmpegPipe&) = delete;

            ~FfmpegPipe()
            {
                (void)close();
            }

            void write(const void* _data, const size_t _size)
            {
                if (!m_pipe)
                    throw std::runtime_error("ffmpeg is gone.");
                if (std::fwrite(_data, 1, _size, m_pipe) != _size)
                {
                    (void)close();
                    throw std::runtime_error("ffmpeg stopped reading frames; it probably failed - rerun without "
                                             "--quiet to see its output.");
                }
            }

            // The exit code, once stdin is closed and ffmpeg has finished writing its file.
            int close()
            {
                if (!m_pipe)
                    return m_exitCode;
#if JUCE_WINDOWS
                m_exitCode = _pclose(m_pipe);
#else
                const auto status = pclose(m_pipe);
                m_exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : (status == -1 ? -1 : 128);
#endif
                m_pipe = nullptr;
                return m_exitCode;
            }

        private:
            std::FILE* m_pipe = nullptr;
            int m_exitCode = 0;
        };

        std::string encodeCommand(const std::string& _ffmpeg, const FrameSize& _render, const FrameSize& _output,
                                  const double _fps, const bool _quiet, const juce::File& _target)
        {
            std::string command = shellQuote(_ffmpeg) + " -hide_banner -loglevel " + (_quiet ? "error" : "warning") +
                " -y -f rawvideo -pixel_format bgra -video_size " + std::to_string(_render.width) + "x" +
                std::to_string(_render.height) + " -framerate " + juce::String(_fps, 6).toStdString() +
                " -i pipe:0";
            if (_output.width != _render.width || _output.height != _render.height)
            {
                const auto w = std::to_string(_output.width), h = std::to_string(_output.height);
                command += " -vf " + shellQuote("scale=" + w + ":" + h + ":force_original_aspect_ratio=decrease,"
                                                "pad=" + w + ":" + h + ":(ow-iw)/2:(oh-ih)/2:color=black");
            }
            // Intermediate video only - the audio is muxed in afterwards, so quality is kept high here.
            return command + " -c:v libx264 -preset veryfast -crf 16 -pix_fmt yuv420p " +
                shellQuote(_target.getFullPathName().toStdString());
        }

        void mux(const std::string& _ffmpeg, const juce::File& _video, const juce::File& _audio,
                 const juce::File& _output, const bool _quiet)
        {
            juce::StringArray args{juce::String::fromUTF8(_ffmpeg.c_str()), "-hide_banner", "-loglevel",
                                   _quiet ? "error" : "warning", "-y", "-i", _video.getFullPathName(), "-i",
                                   _audio.getFullPathName(), "-map", "0:v:0", "-map", "1:a:0", "-c:v", "copy",
                                   "-c:a", "aac", "-b:a", "320k", "-shortest", _output.getFullPathName()};
            juce::ChildProcess ffmpeg;
            if (!ffmpeg.start(args))
                throw std::runtime_error("Cannot start ffmpeg to mux the video.");
            const auto output = ffmpeg.readAllProcessOutput();
            if (!ffmpeg.waitForProcessToFinish(600000) || ffmpeg.getExitCode() != 0)
                throw std::runtime_error("ffmpeg could not mux audio and video:\n" + output.toStdString());
            if (output.isNotEmpty() && !_quiet)
                std::cerr << output;
        }

        // The render reads the player's settings but must not write them back, so it works on a copy
        // that lives for exactly as long as the render does.
        std::unique_ptr<juce::PropertiesFile> copyConfig(const juce::File& _source, const juce::File& _target)
        {
            juce::PropertiesFile::Options options;
            options.applicationName = "DSP56300Emulator_SC88Hardware";
            options.filenameSuffix = ".settings";
            options.folderName = "DSP56300Emulator_SC88Hardware";
            options.osxLibrarySubFolder = "Application Support/DSP56300Emulator_SC88Hardware";
            auto config = std::make_unique<juce::PropertiesFile>(_target, options);
            if (!_source.existsAsFile())
                return config;
            const auto xml = juce::parseXML(_source);
            if (!xml || !xml->hasTagName("PROPERTIES"))
                throw std::runtime_error("Invalid XML config file: " + _source.getFullPathName().toStdString());
            for (const auto* value : xml->getChildWithTagNameIterator("VALUE"))
            {
                const auto name = value->getStringAttribute("name");
                if (name.isEmpty())
                    continue;
                if (const auto* child = value->getFirstChildElement())
                    config->setValue(name, child);
                else
                    config->setValue(name, value->getStringAttribute("val"));
            }
            return config;
        }

        // The same overrides the standalone applies before it builds the player, so a video matches
        // what the app would have played with these options.
        void applyOverrides(const LaunchOptions& _options, juce::PropertiesFile& _config)
        {
            if (_options.has("device"))
                _config.setValue("deviceModel", static_cast<int>(parseDevice(_options.get("device"))));
            if (_options.has("reset"))
                _config.setValue("songResetMode", static_cast<int>(parseReset(_options.get("reset"))));
            if (_options.has("song-gap-ms"))
                _config.setValue("songGapMs", static_cast<int>(_options.number("song-gap-ms", 0)));
            if (_options.has("limiter"))
                _config.setValue("outputLimiter", _options.get("limiter") == "on");
            if (_options.has("factory-reset"))
                _config.setValue("factoryResetOnLoad", _options.get("factory-reset") == "on");
            if (_options.has("fast-boot"))
                _config.setValue("fastBoot", _options.get("fast-boot") == "on");
            if (_options.has("pcm-card"))
                _config.setValue("pcmCardPath", launchFile(_options.get("pcm-card")).getFullPathName());
            if (_options.has("gain"))
                _config.setValue("outputGain", _options.number("gain", 1));
            // Nothing offline opens a port, and a virtual port would outlive nothing useful here.
            _config.setValue("portMidiEnabled", false);
        }

        void writeFrame(FfmpegPipe& _pipe, const juce::Image& _image, std::vector<uint8_t>& _row)
        {
            const juce::Image::BitmapData pixels(_image, juce::Image::BitmapData::readOnly);
            const auto bytesPerRow = static_cast<size_t>(pixels.width) * 4;
            _row.resize(bytesPerRow);
            for (int y = 0; y < pixels.height; ++y)
            {
                // juce pads its rows; ffmpeg wants them packed.
                const auto* line = pixels.getLinePointer(y);
                if (pixels.pixelStride == 4)
                    _pipe.write(line, bytesPerRow);
                else
                {
                    for (int x = 0; x < pixels.width; ++x)
                    {
                        const auto* pixel = line + static_cast<size_t>(x) * pixels.pixelStride;
                        _row[x * 4 + 0] = pixel[0];
                        _row[x * 4 + 1] = pixel[1];
                        _row[x * 4 + 2] = pixel[2];
                        _row[x * 4 + 3] = 0xff;
                    }
                    _pipe.write(_row.data(), bytesPerRow);
                }
            }
        }
    } // namespace

    int renderVideo(const LaunchOptions& _options)
    {
        int errorCode = 3;
        try
        {
            const auto quiet = _options.has("quiet");
            const auto ffmpeg = _options.get("ffmpeg", "ffmpeg");
            const auto videoFile = launchFile(_options.get("video"));
            const auto fps = _options.number("fps", 30);
            const auto render = renderSizeFor(static_cast<int>(_options.number("video-scale", 200)));
            const auto output = _options.has("video-size") ? parseVideoSize(_options.get("video-size")) : render;

            const auto configFile = launchFile(_options.get("config", defaultDataFolder() + "config/88emuPlayer.xml"));
            if (_options.has("config") && !configFile.existsAsFile())
                throw std::runtime_error("Config file not found: " + configFile.getFullPathName().toStdString());
            configureRomSearchPaths(_options);
            requireFfmpeg(ffmpeg);
            if (_options.has("pcm-card"))
                (void)loadPcmCard(launchFile(_options.get("pcm-card")));

            errorCode = 4;
            if (videoFile.exists() && !_options.has("overwrite"))
                throw std::runtime_error("Output already exists; use --overwrite: " +
                                         videoFile.getFullPathName().toStdString());
            if (!videoFile.getParentDirectory().isDirectory())
                throw std::runtime_error("Output directory does not exist.");
            if (videoFile == launchFile(_options.files.front()) || videoFile == configFile)
                throw std::runtime_error("Output must not replace the input or config file.");
            errorCode = 3;

            const juce::TemporaryFile configCopy(".xml");
            auto config = copyConfig(configFile, configCopy.getFile());
            applyOverrides(_options, *config);

            // The player reads both of these while it builds itself, exactly as it does in the app.
            standaloneLaunch = &_options;
            standaloneConfig = config.get();
            struct ScopedLaunchGlobals
            {
                ~ScopedLaunchGlobals() { standaloneLaunch = nullptr; standaloneConfig = nullptr; }
            } scopedLaunchGlobals;

            Processor processor;
            if (!processor.hasValidRom())
            {
                const auto inventory = emu88Lib::RomLoader::scan();
                const auto device = emu88Lib::RomLoader::toRomDevice(processor.deviceModel());
                throw std::runtime_error("Missing ROMs for " + std::string(deviceId(processor.deviceModel())) + ":\n" +
                                         inventory.describeRequirements(device));
            }
            if (const auto warnings = emu88Lib::RomLoader::scan().warnings(
                    emu88Lib::RomLoader::toRomDevice(processor.deviceModel()));
                !warnings.empty())
                std::cerr << "ROM warning:\n" << warnings;

            auto& player = processor.midiPlayer();
            const auto loaded = player.addFiles(_options.files);
            if (!loaded.errors.empty())
                throw std::runtime_error(loaded.errors.front());
            const auto tailMs = _options.number("tail-ms", 4000);
            player.setEndTailMs(static_cast<uint32_t>(tailMs));

            const auto savedAudio = config->getXmlValue("audioSetup");
            const auto sampleRate = _options.number(
                "sample-rate", savedAudio ? savedAudio->getDoubleAttribute("audioDeviceRate", 44100) : 44100);
            if (!std::isfinite(sampleRate) || sampleRate < 8000 || sampleRate > 192000 ||
                std::floor(sampleRate) != sampleRate)
                throw std::runtime_error("Invalid output sample rate in config; override with --sample-rate.");

            constexpr int blockSize = 256;
            // The highest quality resampler, as the WAV render uses: an offline render has no CPU
            // budget to keep to, and this keeps the audio next to a video identical to --output on
            // its own.
            processor.setResamplerMode(synthLib::Resampler::Mode::MameHq);
            processor.setPlayConfigDetails(0, 2, sampleRate, blockSize);
            processor.prepareToPlay(sampleRate, blockSize);

            Editor editor(processor, true);
            editor.setSize(render.width, render.height);
            auto* rml = editor.rmlComponent();
            if (!rml)
                throw std::runtime_error("Unable to create the player UI.");

            errorCode = 4;
            const auto bits = static_cast<int>(_options.number("bits", 24));
            const auto wavFile = _options.has("output") ? launchFile(_options.get("output"))
                                                        : videoFile.withFileExtension(".render.wav");
            if (_options.has("output") && wavFile.exists() && !_options.has("overwrite"))
                throw std::runtime_error("Output already exists; use --overwrite: " +
                                         wavFile.getFullPathName().toStdString());
            const juce::TemporaryFile wav(wavFile);
            auto stream = wav.getFile().createOutputStream();
            if (!stream || !stream->openedOk())
                throw std::runtime_error("Cannot create the audio track.");
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                format.createWriterFor(stream.get(), sampleRate, 2, bits, {}, 0));
            if (!writer)
                throw std::runtime_error("Cannot create WAV writer.");
            stream.release(); // Writer owns it after successful construction.

            juce::AudioBuffer<float> audio(2, blockSize);
            juce::MidiBuffer midi;
            auto process = [&](const int _count)
            {
                audio.setSize(2, _count, false, false, true);
                midi.clear();
                processor.processBlock(audio, midi);
                processor.handleAsyncUpdate(); // A device rate change would otherwise wait for a message loop.
            };

            if (!quiet)
                std::cerr << "Booting " << deviceId(processor.deviceModel()) << "...\n";
            for (auto remaining = static_cast<int64_t>(std::ceil(_options.number("boot-ms", 5000) * sampleRate / 1000));
                 remaining > 0;)
            {
                const auto count = static_cast<int>(std::min<int64_t>(blockSize, remaining));
                process(count);
                remaining -= count;
            }

            const auto duration = player.preparationSeconds(0) + player.entries().front().durationSeconds +
                tailMs / 1000;
            if (!std::isfinite(duration) || duration < 0 || duration > 1.0e12)
                throw std::runtime_error("Invalid or excessive song duration.");
            auto totalSamples = static_cast<uint64_t>(std::ceil(duration * sampleRate)) + 1;
            if (_options.has("max-seconds"))
                totalSamples = std::min<uint64_t>(
                    totalSamples, static_cast<uint64_t>(std::ceil(_options.number("max-seconds", 0) * sampleRate)));
            if (totalSamples > (0xffffffffull - 65536) / (2 * static_cast<uint64_t>(bits / 8)))
                throw std::runtime_error("Render exceeds RIFF WAV size limit; reduce --max-seconds or sample rate.");
            const auto totalFrames = static_cast<uint64_t>(std::ceil(static_cast<double>(totalSamples) * fps /
                                                                     sampleRate));

            if (!quiet)
                std::cerr << "Rendering " << totalFrames << " frames at " << render.width << 'x' << render.height
                          << ", " << fps << " fps...\n";

            const juce::TemporaryFile videoTrack(videoFile.withFileExtension(".track.mp4"));
            FfmpegPipe pipe(encodeCommand(ffmpeg, render, output, fps, quiet, videoTrack.getFile()));

            juce::Image frame(juce::Image::ARGB, render.width, render.height, true);
            std::vector<uint8_t> row;
            uint64_t done = 0, clipped = 0;
            int lastPercent = -1;
            player.play(0);
            for (uint64_t index = 0; index < totalFrames; ++index)
            {
                // Whole blocks, in the same sequence the WAV render uses, so both produce the same
                // audio sample for sample. A frame is drawn from the state the block that reached
                // its point in time left behind, which puts the picture at most one block - a fifth
                // of a frame at 30 fps - behind the sound it belongs to.
                const auto frameEnd = std::min<uint64_t>(
                    totalSamples, static_cast<uint64_t>(std::llround(static_cast<double>(index + 1) * sampleRate / fps)));
                while (done < frameEnd)
                {
                    const auto count = static_cast<int>(std::min<uint64_t>(blockSize, totalSamples - done));
                    process(count);
                    for (int channel = 0; channel < 2; ++channel)
                    {
                        auto* samples = audio.getWritePointer(channel);
                        for (int i = 0; i < count; ++i)
                        {
                            if (!std::isfinite(samples[i]))
                                throw std::runtime_error("Non-finite audio sample.");
                            if (std::abs(samples[i]) > 1)
                                ++clipped;
                            if (bits != 32)
                                samples[i] = std::clamp(samples[i], -1.0f, 1.0f);
                        }
                    }
                    if (!writer->writeFromAudioSampleBuffer(audio, 0, count))
                        throw std::runtime_error("WAV write failed (disk full?).");
                    done += count;
                }

                // The UI reads the board's display as the player's own timer does, and the frame is
                // drawn for the point in time those samples ended at.
                editor.updateFromDevice();
                if (!rml->renderOffline(frame, static_cast<double>(done) / sampleRate))
                    throw std::runtime_error("The player UI cannot render offline.");
                writeFrame(pipe, frame, row);

                const auto percent = static_cast<int>((index + 1) * 100 / totalFrames);
                if (!quiet && percent / 10 != lastPercent / 10)
                    std::cerr << percent << "%\n";
                lastPercent = percent;
            }

            if (!writer->flush())
                throw std::runtime_error("WAV flush failed.");
            writer.reset();
            if (const auto code = pipe.close())
                throw std::runtime_error("ffmpeg failed to encode the video (exit code " + std::to_string(code) + ").");

            // Muxed aside and moved into place afterwards, so a failure here cannot damage a video
            // that is already there.
            const juce::TemporaryFile finished(videoFile);
            mux(ffmpeg, videoTrack.getFile(), wav.getFile(), finished.getFile(), quiet);
            if (!publishFile(finished, videoFile, _options.has("overwrite")))
                throw std::runtime_error(
                    "Cannot publish the completed video: destination exists or the filesystem refused publication.");
            if (_options.has("output") && !publishFile(wav, wavFile, _options.has("overwrite")))
                throw std::runtime_error("Cannot publish the WAV next to the video.");

            if (clipped)
                std::cerr << "Warning: " << clipped
                          << (bits == 32 ? " samples exceed full scale (preserved in the WAV).\n"
                                         : " samples clipped; reduce --gain.\n");
            if (!quiet)
                std::cerr << "Wrote " << totalFrames << " frames to " << videoFile.getFullPathName() << '\n';
            return 0;
        }
        catch (const std::exception& error)
        {
            std::cerr << error.what() << '\n';
            return errorCode;
        }
    }
}
