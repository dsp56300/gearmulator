#include "midiFile.h"

#include "juce_core/juce_core.h"

namespace jucePlayer::midiFile
{
    bool isSupported(const std::string& _path)
    {
        const auto extension =
            juce::File::getCurrentWorkingDirectory().getChildFile(_path).getFileExtension().toLowerCase();
        return extension == ".mid" || extension == ".midi" || extension == ".rcp" || extension == ".r36" ||
            extension == ".g36";
    }

    bool read(const std::string& _path, std::vector<synthLib::midi::Event>& _events, std::string& _error)
    {
        _events.clear();
        _error.clear();
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(_path);
        const auto extension = file.getFileExtension().toLowerCase();
        if (!isSupported(_path))
        {
            _error = file.getFileName().toStdString() + ": expected a .mid, .midi, .rcp, .r36, or .g36 file";
            return false;
        }
        juce::MemoryBlock contents;
        if (!file.loadFileAsData(contents))
        {
            _error = "Cannot read '" + _path + "'";
            return false;
        }
        std::vector<uint8_t> data(contents.getSize());
        if (!data.empty())
            contents.copyTo(data.data(), 0, data.size());
        const bool smf = extension == ".mid" || extension == ".midi";
        if (!(smf ? synthLib::midi::readSmf(data, _events, _error) : synthLib::midi::readRcp(data, _events, _error)))
        {
            _error = file.getFileName().toStdString() + ": " + _error;
            return false;
        }
        return true;
    }
} // namespace jucePlayer::midiFile
