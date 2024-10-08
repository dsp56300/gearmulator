#include "unpacker.h"

#include "utils.h"

#include "dsp56kEmu/logging.h"

#include <vector>

namespace virusLib
{
	bool ROMUnpacker::isValidInstaller(std::istream& _file)
	{
		_file.seekg(0);
		char magic[5] = {0};
		_file.read(magic, 4);
		_file.seekg(-4, std::ios_base::cur);  // restore cursor
		return std::string(magic) == "FORM";
	}

	ROMUnpacker::Firmware ROMUnpacker::getFirmware(std::istream& _file, const DeviceModel _model)
	{
		// First round of extraction
		// This will yield the installer files (html, lcd firmware, vti firmware, etc)
		std::vector<Chunk> chunks = getChunks(_file);

		// The first chunk should be the _file table containing filenames
		Chunk table = chunks[0];
		if (std::string(table.name) != "TABL")
		{
			LOG("Installer file table not found");
			return Firmware{};
		}

		// Read all the filenames (starting at offset 1)
		std::vector<std::string> filenames{};
		std::string currentFilename;
		for (auto it = begin(table.data) + 1; it != end(table.data); ++it)
		{
			if (*it == '\0')
			{
				filenames.emplace_back(currentFilename);
				currentFilename.clear();
			} else
			{
				currentFilename += *it;
			}
		}

		uint8_t tableItemCount = table.data[0];
		assert (filenames.size() == tableItemCount);
		assert (chunks.size() - 1 == tableItemCount);

		// Find the VTI _file for the given _model
		const std::string vtiFilename = getVtiFilename(_model);
		Chunk* vti = nullptr;
		for (auto& f: filenames)
		{
			// Find the matching chunk for this filename
			auto i = &f - &filenames[0] + 1;
			LOG("Chunk " << chunks[i].name << " = " << f << " (size=0x" << HEX(chunks[i].size) << ")");
			if (f == vtiFilename)
			{
				vti = &chunks[i];
			}
		}

		if (!vti)
		{
			LOG("Could not find the VTI _file");
			return Firmware{};
		}

		// Second round of extraction
		// This will yield the packed chunks of the VTI _file containing F.bin, S.bin, P.bin
		LOG("Found VTI: " << vtiFilename << " in chunk " << vti->name << ", size=0x" << HEX(vti->data.size()));
		imemstream stream(vti->data);

		std::vector<Chunk> parts = getChunks(stream);
		if (parts.empty())
		{
			return Firmware{};
		}

		// Perform unpacking for all roms
		std::vector<char> dsp = unpackFile(parts, 'F');
		std::vector<std::vector<char>> presets{};
		for (const char& presetFileId: {'P', 'S'})
		{
			auto presetFile = unpackFile(parts, presetFileId);
			if (!presetFile.empty())
			{
				presets.emplace_back(presetFile);
			}
		}

		return Firmware{dsp, presets};
	}

	std::string ROMUnpacker::getVtiFilename(const DeviceModel _model)
	{
		switch (_model)
		{
		case DeviceModel::TI:
			return "vti.bin";
		case DeviceModel::TI2:
			return "vti_2.bin";
		case DeviceModel::Snow:
			return "vti_snow.bin";
		default:
			return {};
		}
	}

	std::vector<ROMUnpacker::Chunk> ROMUnpacker::getChunks(std::istream& _file)
	{
		std::vector<Chunk> result;
		char filename[5] = {0};
		uint32_t filesize;
		_file.read(filename, 4);
		_file.read(reinterpret_cast<char*>(&filesize), 4);
		filesize = swap32(filesize);

		LOG("FileID: " << filename);
		LOG("FileSize: " << filesize);

		while (!_file.eof())
		{
			Chunk chunk{};
			_file.read(chunk.name, 4);
			_file.read(reinterpret_cast<char*>(&chunk.size), 4);
			chunk.size = swap32(chunk.size);
			if (chunk.size > 0)
			{
				chunk.data.resize(chunk.size);
				_file.read(chunk.data.data(), chunk.size);
				result.emplace_back(chunk);
			}
		}

		return result;
	}

	std::vector<char> ROMUnpacker::unpackFile(std::vector<Chunk>& _chunks, const char _fileId)
	{
		std::vector<char> content;

		for (auto& chunk: _chunks)
		{
			if (chunk.name[0] != _fileId)
			{
				continue;
			}

			if(chunk.data.size() % 35 == 2)
			{
				size_t ctr = 0;

				// 4 chunk ID, 4 chunk length, 1 block id?, 2 segment offset in bytes (32)
				// each chunk has 2 remaining bytes (checksum?)
				for (size_t i = 0; i < chunk.size - 2; i += 35)
				{
					const auto idx = swap16(*reinterpret_cast<uint16_t*>(&chunk.data[i + 1]));
					assert (idx == ctr);
					for (size_t j = 0; j < 32; ++j)
					{
						content.emplace_back(chunk.data[i + j + 3]);
					}
					ctr += 32;
				}
			}
			else
			{
				// slightly different chunk format
				// 4 chunk ID, 4 chunk length, 1 chunk id, 1 counter per segment, 1 block id?
				assert(chunk.data.size() % 35 == 0);

				uint8_t ctr = 0;
				for (size_t i = 0; i < chunk.size - 2; i += 35)
				{
					const auto idx = static_cast<uint8_t>(chunk.data[i + 1]);

					assert (idx == ctr);
					for (size_t j = 0; j < 32; ++j)
					{
						content.emplace_back(chunk.data[i + j + 3]);
					}
					++ctr;
				}
			}
		}

		return content;
	}
}
