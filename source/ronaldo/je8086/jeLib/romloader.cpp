#include "romloader.h"

#include "baseLib/filesystem.h"

#include <algorithm>

#include "state.h"

#include "baseLib/binarystream.h"

#include "synthLib/midiToSysex.h"

#include "dsp56kBase/logging.h"

namespace jeLib
{
	static constexpr size_t g_fileSizeKeyboardMidi = 79372;
	static constexpr size_t g_fileSizeRackMidi = 79373;	// 14 files total, first 8 have the same size as keyboard

	static constexpr std::initializer_list<const char*> g_keyboardChecksums = {"49F8", "12E3", "A146", "E6EB", "5455", "672D", "C131", "F294"};
	static constexpr std::initializer_list<const char*> g_rackChecksums = {"EE52", "33A0", "E3CD", "11F1", "C8D9", "A0F8", "E7DB", "61D8", "18FF", "CFFC", "658A", "0DDA", "A88C", "CCAB"};

	static constexpr size_t g_fileCountKeyboardMidi = std::size(g_keyboardChecksums);
	static constexpr size_t g_fileCountRackMidi = std::size(g_rackChecksums);

	Rom RomLoader::findROM()
	{
		auto roms = findROMs();
		if (roms.empty())
			return {};
		return roms.front();
	}

	std::vector<Rom> RomLoader::findROMs()
	{
		std::vector<Rom> results;

		auto append = [&results](Rom&& _r)
		{
			if (_r.isValid())
				results.emplace_back(std::move(_r));
		};

		auto files = synthLib::RomLoader::findFiles(".mid", g_fileSizeKeyboardMidi, g_fileSizeKeyboardMidi);

		if (files.size() >= g_fileCountKeyboardMidi)
		{
			auto filesRack = synthLib::RomLoader::findFiles(".mid", g_fileSizeRackMidi, g_fileSizeRackMidi);

			files.insert(files.end(), filesRack.begin(), filesRack.end());

			std::vector<MidiData> fileDatasKeyboard;
			std::vector<MidiData> fileDatasRack;

			loadFromMidiFiles(fileDatasKeyboard, fileDatasRack, files);

			if (fileDatasKeyboard.size() == g_fileCountKeyboardMidi)
				append(loadFromMidiFiles(fileDatasKeyboard));

			if (fileDatasRack.size() == g_fileCountRackMidi)
				append(loadFromMidiFiles(fileDatasRack));
		}

		files = synthLib::RomLoader::findFiles(".bin", Rom::RomSizeKeyboard, Rom::RomSizeKeyboard);
		for (const auto& f : files)
			append(Rom(f));

		files = synthLib::RomLoader::findFiles(".bin", Rom::RomSizeRack, Rom::RomSizeRack);
		for (const auto& f : files)
			append(Rom(f));
		return results;
	}

	Rom RomLoader::loadFromMidiFiles(const std::vector<MidiData>& _files)
	{
		Range totalRange = { std::numeric_limits<uint32_t>::max(), 0 };

		std::vector<uint8_t> fullRom;

		for (const auto& file : _files)
		{
			std::vector<std::vector<uint8_t>> sysexMessages;

			if (!synthLib::MidiToSysex::extractSysexFromData(sysexMessages, file.second))
				return {};

			for (const auto& message : sysexMessages)
			{
				auto range = parseSysexDump(fullRom, message);
				if (range.second <= range.first)
					return {};

				totalRange.first = std::min(totalRange.first, range.first);
				totalRange.second = std::max(totalRange.second, range.second);
			}
		}

		fullRom.erase(fullRom.begin() + totalRange.second, fullRom.end());
		fullRom.erase(fullRom.begin(), fullRom.begin() + totalRange.first);

//		baseLib::filesystem::writeFile("D:\\je8086_rom.bin", fullRom);

		if (fullRom.size() != Rom::RomSizeKeyboard && fullRom.size() != Rom::RomSizeRack)
			return {};

		// come up with a useful name
		using namespace baseLib::filesystem;

		std::vector<std::string> sortedFiles;
		sortedFiles.reserve(_files.size());

		for (const auto& [name, data] : _files)
			sortedFiles.push_back(name);

		std::sort(sortedFiles.begin(), sortedFiles.end());

		const auto ext = getExtension(sortedFiles.front());
		const auto firstName = stripExtension(getFilenameWithoutPath(sortedFiles.front()));
		const auto lastName = stripExtension(getFilenameWithoutPath(sortedFiles.back()));

		const auto name = firstName + "..." + lastName + ext;

		return Rom(fullRom, name);
	}

	void RomLoader::loadFromMidiFiles(std::vector<MidiData>& _filesKeyboard, std::vector<MidiData>& _filesRack, const std::vector<std::string>& _files)
	{
		for (const auto& file : _files)
		{
			std::vector<uint8_t> data;
			if (!baseLib::filesystem::readFile(data, file))
				continue;

			// check if for keyboard or rack via checksum
			// find string "CHECK SUM = " in the file
			constexpr char key[] = "CHECK SUM = ";
			constexpr size_t keySize = std::size(key) - 1;
			auto it = std::search(data.begin(), data.end(), key, key + keySize);
			if (it == data.end())
				continue;
			it += keySize;
			std::string checksum;
			while (it != data.end() && checksum.size() < 4)
				checksum.push_back(static_cast<char>(*it++));

			if (checksum.size() != 4)
				continue;

			bool found = false;

			for (const char* cs : g_keyboardChecksums)
			{
				if (checksum == cs)
				{
					_filesKeyboard.emplace_back(file, std::move(data));
					found = true;
					break;
				}
			}

			if (found)
				continue;

			for (const char* cs : g_rackChecksums)
			{
				if (checksum == cs)
				{
					_filesRack.emplace_back(file, std::move(data));  // NOLINT(bugprone-use-after-move)
					break;
				}
			}
		}
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
