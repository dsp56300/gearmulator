#include "romloader.h"

#include "rackBootBlock.h"

#include "baseLib/filesystem.h"

#include <algorithm>
#include <set>

#include "state.h"

#include "baseLib/md5.h"

#include "synthLib/midiToSysex.h"

#include "dsp56kBase/logging.h"

namespace jeLib
{
	// The eight JP-8000 update SMFs hold its whole flash. The JP-8080 ones, some of the same size, only hold a firmware
	// body that cannot boot, the JP-8080 needs a complete 1 MB flash dump
	static constexpr size_t g_fileSizeKeyboardMidi = 79372;
	static constexpr std::initializer_list<const char*> g_keyboardChecksums = {"49F8", "12E3", "A146", "E6EB", "5455", "672D", "C131", "F294"};

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
		std::set<baseLib::MD5> found;

		auto append = [&](Rom&& _r)
		{
			if (_r.isValid() && found.insert(baseLib::MD5(_r.getData())).second)
				results.emplace_back(std::move(_r));
		};

		std::vector<MidiData> keyboardMidi;
		loadMidiFiles(keyboardMidi, synthLib::RomLoader::findFiles(".mid", g_fileSizeKeyboardMidi, g_fileSizeKeyboardMidi));
		if (keyboardMidi.size() == std::size(g_keyboardChecksums))
			append(loadFromMidiFiles(keyboardMidi));

		auto appendFiles = [&](const size_t _size)
		{
			auto files = synthLib::RomLoader::findFilesRecursive(".bin", _size, _size);
			std::sort(files.begin(), files.end(), [](const auto& _a, const auto& _b) { return _a.path < _b.path; });
			for (const auto& f : files)
				append(Rom(f.path));
		};

		appendFiles(Rom::RomSizeKeyboard);
		appendFiles(Rom::RomSizeRack);

		return results;
	}

	// The generated JP-8080 boot block is experimental: the test console uses it, findROMs() does not
	Rom RomLoader::loadROM(const std::string& _filename)
	{
		return makeBootable(Rom(_filename));
	}

	Rom RomLoader::makeBootable(Rom&& _rom)
	{
		if (_rom.getData().size() != Rom::RomSizeRackFirmware)
			return std::move(_rom);
		auto image = createRackBootImage(_rom.getData());
		if (image.empty())
			return std::move(_rom);
		return Rom(image, _rom.getName());
	}

	Rom RomLoader::loadFromMidiFiles(const std::vector<MidiData>& _files)
	{
		Range totalRange = { std::numeric_limits<uint32_t>::max(), 0 };

		std::vector<uint8_t> fullRom;

		for (const auto& file : _files)
		{
			synthLib::SysexBuffer fileBuf(file.second.begin(), file.second.end());
			synthLib::SysexBufferList sysexMessages;

			if (!synthLib::MidiToSysex::extractSysexFromData(sysexMessages, fileBuf))
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

		if (fullRom.size() != Rom::RomSizeKeyboard && fullRom.size() != Rom::RomSizeRackFirmware)
			return {};

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

	void RomLoader::loadMidiFiles(std::vector<MidiData>& _keyboard, const std::vector<std::string>& _files)
	{
		std::set<std::string> checksums;

		for (const auto& file : _files)
		{
			std::vector<uint8_t> data;
			if (!baseLib::filesystem::readFile(data, file))
				continue;

			constexpr char key[] = "CHECK SUM = ";
			constexpr size_t keySize = std::size(key) - 1;
			auto it = std::search(data.begin(), data.end(), key, key + keySize);
			if (it == data.end())
				continue;
			it += keySize;
			std::string checksum;
			while (it != data.end() && checksum.size() < 4)
				checksum.push_back(static_cast<char>(*it++));

			if (!checksums.insert(checksum).second)
				continue;
			if (std::find(g_keyboardChecksums.begin(), g_keyboardChecksums.end(), checksum) != g_keyboardChecksums.end())
				_keyboard.emplace_back(file, std::move(data));
		}
	}

	RomLoader::Range RomLoader::parseSysexDump(std::vector<uint8_t>& _fullRom, const synthLib::SysexBuffer& _sysex)
	{
		Range result = { std::numeric_limits<uint32_t>::max(), 0 };

		try
		{
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
