#include "romloader.h"

#include "baseLib/filesystem.h"

#include <algorithm>

#include <cstdio>

#include "state.h"

#include "baseLib/binarystream.h"

#include "synthLib/midiToSysex.h"

#include "dsp56kEmu/logging.h"

namespace jeLib
{
	static constexpr size_t FileSizeKeyboardMidi = 79372;
	static constexpr size_t FileSizeRackMidi = 79373;	// 14 files total, first 8 have the same size as keyboard
	static constexpr size_t FileCountKeyboardMidi = 8;
	static constexpr size_t FileCountRackMidi = 6;

	Rom RomLoader::findROM()
	{
		auto files = synthLib::RomLoader::findFiles(".mid", FileSizeKeyboardMidi, FileSizeKeyboardMidi);
		if (!files.empty() && files.size() == FileCountKeyboardMidi)
		{
			auto filesRack = synthLib::RomLoader::findFiles(".mid", FileSizeRackMidi, FileSizeRackMidi);

			if (!filesRack.empty() && filesRack.size() == FileCountRackMidi)
			{
				files.insert(files.end(), filesRack.begin(), filesRack.end());
			}
			return loadFromMidiFiles(files);
		}

		files = synthLib::RomLoader::findFiles(".bin", Rom::RomSizeKeyboard, Rom::RomSizeKeyboard);
		if (!files.empty())
			return Rom(files.front());

		files = synthLib::RomLoader::findFiles(".bin", Rom::RomSizeRack, Rom::RomSizeRack);
		if (!files.empty())
			return Rom(files.front());
		return {};
	}

	Rom RomLoader::loadFromMidiFiles(const std::vector<std::string>& _files)
	{
		std::vector<std::vector<uint8_t>> sysexMessages;

		for (const auto& file : _files)
		{
			if (!synthLib::MidiToSysex::extractSysexFromFile(sysexMessages, file))
				return {};
		}

		Range totalRange = { std::numeric_limits<uint32_t>::max(), 0 };

		std::vector<uint8_t> fullRom;

		for (const auto& message : sysexMessages)
		{
			auto range = parseSysexDump(fullRom, message);
			if (range.second <= range.first)
				return {};

			totalRange.first = std::min(totalRange.first, range.first);
			totalRange.second = std::max(totalRange.second, range.second);
		}

		fullRom.erase(fullRom.begin() + totalRange.second, fullRom.end());
		fullRom.erase(fullRom.begin(), fullRom.begin() + totalRange.first);

//		baseLib::filesystem::writeFile("D:\\je8086_rom.bin", fullRom);

		return Rom(fullRom, baseLib::filesystem::getFilenameWithoutPath(_files.front()));
	}

	RomLoader::Range RomLoader::parseSysexDump(std::vector<uint8_t>& _fullRom, const std::vector<uint8_t>& _sysex)
	{
		Range result = { std::numeric_limits<uint32_t>::max(), 0 };

		try
		{
			baseLib::BinaryStream f;
			f.write(_sysex);

			const auto addr4 = State::getAddress4(_sysex);

			if (!rLib::Storage::isValid(addr4))
				return {};

			constexpr auto headerSize = std::size(g_sysexHeader) + 1 /* cmd */ + addr4.size();
			constexpr auto footerSize = std::size(g_sysexFooter);

			const auto len = _sysex.size() - headerSize - footerSize;

			const auto offset = rLib::Storage::toLinearAddress(addr4);

			result.first = std::min(result.first, offset);

			if (State::calcChecksum(_sysex) != _sysex[_sysex.size()-2])
				return {};

			const auto requiredOutSize = offset + len * 7 / 8;

			if (_fullRom.size() < requiredOutSize)
				_fullRom.resize(requiredOutSize, 0xff);

			uint32_t outIndex = offset;
			uint32_t inIndex = headerSize;

			for (size_t i = 0; i < len; i += 8)
			{
				std::array<uint8_t, 7> buffer;

				const auto topbits = _sysex[inIndex++];
				const auto todo = std::min<size_t>(len - i - 1, buffer.size());

				for (size_t j = 0; j < todo; j++)
					buffer[j] = _sysex[inIndex++];

				for (size_t j = 0; j < todo; j++)
				{
					if (topbits & (1 << j))
						buffer[j] |= 0x80;
					_fullRom[outIndex++] = buffer[j];
				}
			}

			result.second = std::max(result.second, outIndex);

			return result;
		}
		catch (std::range_error& e)
		{
			LOG("Exception reading sysex dump: " << e.what());
			return {};
		}
	}
}
