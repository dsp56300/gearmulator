#include "88emuplayer/app/Emu88Playlist.h"

#include "88emuplayer/app/Emu88LaunchOptions.h"

namespace emu88Player::playlist
{
    bool isSupported(const std::string& _path)
    {
        const auto file = juce::File(_path);
        return file.hasFileExtension("m3u") || file.hasFileExtension("m3u8");
    }

    juce::File defaultFile()
    {
        return juce::File(defaultDataFolder()).getChildFile("playlist/last-session.m3u8");
    }

    bool read(const juce::File& _file, std::vector<std::string>& _paths, std::string& _error)
    {
        _paths.clear();
        if (!_file.existsAsFile())
        {
            _error = "Playlist file does not exist: " + _file.getFullPathName().toStdString();
            return false;
        }

        // A playlist that exists but cannot be opened must not pass for an empty one.
        juce::FileInputStream stream(_file);
        if (stream.failedToOpen())
        {
            _error = "Unable to read playlist " + _file.getFullPathName().toStdString() + ": " +
                stream.getStatus().getErrorMessage().trim().toStdString();
            return false;
        }

        const auto base = _file.getParentDirectory();
        for (const auto& line : juce::StringArray::fromLines(stream.readEntireStreamAsString()))
        {
            const auto path = line.trim();
            if (path.isEmpty() || path.startsWithChar('#'))
                continue;
            _paths.push_back((juce::File::isAbsolutePath(path) ? juce::File(path) : base.getChildFile(path))
                                 .getFullPathName()
                                 .toStdString());
        }
        return true;
    }

    bool write(const juce::File& _file, const std::vector<jucePlayer::MidiPlayer::Entry>& _entries,
               std::string& _error)
    {
        const auto parent = _file.getParentDirectory();
        if (!parent.createDirectory().wasOk())
        {
            _error = "Unable to create playlist folder: " + parent.getFullPathName().toStdString();
            return false;
        }

        juce::StringArray lines;
        lines.add("#EXTM3U");
        for (const auto& entry : _entries)
            lines.add(juce::File(entry.path).getRelativePathFrom(parent));

        juce::TemporaryFile temporary(_file);
        if (!temporary.getFile().replaceWithText(lines.joinIntoString("\n") + "\n") ||
            !temporary.overwriteTargetFileWithTemporary())
        {
            _error = "Unable to write playlist: " + _file.getFullPathName().toStdString();
            return false;
        }
        return true;
    }
} // namespace emu88Player::playlist
