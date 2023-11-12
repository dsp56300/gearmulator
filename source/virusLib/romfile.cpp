#include <cassert>
#include <fstream>
#include <algorithm>

#include "romfile.h"
#include "utils.h"

#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/logging.h"

#include "../synthLib/os.h"

#include "midiFileToRomData.h"

#include <cstring>	// memcpy

#include "dsp56kEmu/memory.h"

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace virusLib
{
ROMFile::ROMFile(const std::string& _path, const Model _model/* = Model::ABC*/)
{
	if(!_path.empty())
	{
		m_romFileName = _path;

		// Open file
		LOG("Loading ROM at " << _path);

		if(!synthLib::readFile(m_romFileData, _path))
		{
			LOG("Failed to load ROM at '" << _path << "'");
	#ifdef _WIN32
			const auto errorMessage = std::string("Failed to load ROM file. Make sure it is put next to the plugin and ends with .bin");
			::MessageBoxA(nullptr, errorMessage.c_str(), "ROM not found", MB_OK);
	#endif
			return;
		}
	}
	else
	{
		const auto expectedSize = _model == Model::ABC ? getRomSizeModelABC() : 0;

		if(!loadROMData(m_romFileName, m_romFileData, expectedSize, expectedSize))
			return;
	}

	if(initialize())
		printf("ROM File: %s\n", m_romFileName.c_str());
}

ROMFile::ROMFile(std::vector<uint8_t> _data) : m_romFileData(std::move(_data))
{
	initialize();
}

std::string ROMFile::findROM()
{
	return synthLib::findROM(getRomSizeModelABC());
}

bool ROMFile::loadROMData(std::string& _loadedFile, std::vector<uint8_t>& _loadedData, const size_t _expectedSizeMin, const size_t _expectedSizeMax)
{
	// try binary roms first
	const auto file = synthLib::findROM(_expectedSizeMin, _expectedSizeMax);

	if(!file.empty())
	{
		if(synthLib::readFile(_loadedData, file) && !_loadedData.empty())
		{
			_loadedFile = file;
			return true;
		}
	}

	// if that didn't work, load an OS update as rom
	auto loadMidiAsRom = [&](const std::string& _path)
	{
		_loadedFile.clear();
		_loadedData.clear();

		std::vector<std::string> files;

		synthLib::findFiles(files, _path, ".mid", 512 * 1024, 600 * 1024);
		
		bool gotSector0 = false;
		bool gotSector8 = false;

		for (const auto& f : files)
		{
			MidiFileToRomData loader;
			if(!loader.load(f) || loader.getData().size() != getRomSizeModelABC() / 2)
				continue;
			if(loader.getFirstSector() == 0)
			{
				if(gotSector0)
					continue;
				gotSector0 = true;
				_loadedFile = f;
				_loadedData.insert(_loadedData.begin(), loader.getData().begin(), loader.getData().end());
			}
			else if(loader.getFirstSector() == 8)
			{
				if(gotSector8)
					continue;
				gotSector8 = true;
				_loadedData.insert(_loadedData.end(), loader.getData().begin(), loader.getData().end());
			}
		}
		return gotSector0 && _loadedData.size() >= getRomSizeModelABC() / 2;
	};

	if(loadMidiAsRom(synthLib::getModulePath()))
		return true;

	return loadMidiAsRom(synthLib::getModulePath(false));
}

bool ROMFile::initialize()
{
	const std::unique_ptr<std::istream> dsp(new imemstream(reinterpret_cast<std::vector<char>&>(m_romFileData)));
	
	const auto chunks = readChunks(*dsp);

	if (chunks.empty())
		return false;

	bootRom.size = chunks[0].items[0];
	bootRom.offset = chunks[0].items[1];
	bootRom.data = std::vector<uint32_t>(bootRom.size);

	// The first chunk contains the bootrom
	uint32_t i = 2;
	for (; i < bootRom.size + 2; i++)
	{
		bootRom.data[i-2] = chunks[0].items[i];
	}

	// The rest of the chunks is made up of the command stream
	for (size_t j = 0; j < chunks.size(); j++)
	{
		for (; i < chunks[j].items.size(); i++)
			m_commandStream.emplace_back(chunks[j].items[i]);
		i = 0;
	}

	printf("Program BootROM size = 0x%x\n", bootRom.size);
	printf("Program BootROM offset = 0x%x\n", bootRom.offset);
	printf("Program CommandStream size = 0x%x\n", static_cast<uint32_t>(m_commandStream.size()));

	return true;
}

std::vector<ROMFile::Chunk> ROMFile::readChunks(std::istream& _file)
{
	_file.seekg(0, std::ios_base::end);
	const auto fileSize = _file.tellg();

	uint32_t offset = 0x18000;
	int lastChunkId = 4;

	if (fileSize == getRomSizeModelABC() || fileSize == getRomSizeModelABC()/2)	// the latter is a ROM without presets
	{
		// ABC
		m_model = Model::ABC;
	}
	else 
	{
		LOG("Invalid ROM, unexpected filesize");
		return {};
	}

	std::vector<Chunk> chunks;
	chunks.reserve(lastChunkId + 1);

	// Read all the chunks
	for (int i = 0; i <= lastChunkId; i++)
	{
		_file.seekg(offset);

		// Read buffer
		Chunk chunk;
		//file.read(reinterpret_cast<char *>(&chunk->chunk_id), 1);
		_file.read(reinterpret_cast<char*>(&chunk.chunk_id), 1);
		_file.read(reinterpret_cast<char*>(&chunk.size1), 1);
		_file.read(reinterpret_cast<char*>(&chunk.size2), 1);

		if(i == 0 && chunk.chunk_id == 3 && lastChunkId == 4)	// Virus A has one chunk less
			lastChunkId = 3;

		assert(chunk.chunk_id == lastChunkId - i);

		// Format uses a special kind of size where the first byte should be decreased by 1
		const uint16_t len = ((chunk.size1 - 1) << 8) | chunk.size2;

		for (uint32_t j = 0; j < len; j++)
		{
			uint8_t buf[3];
			_file.read(reinterpret_cast<char*>(buf), 3);
			chunk.items.emplace_back((buf[0] << 16) | (buf[1] << 8) | buf[2]);
		}

		chunks.emplace_back(chunk);

		offset += 0x8000;
	}

	return chunks;
}

std::thread ROMFile::bootDSP(dsp56k::DSP& dsp, dsp56k::Peripherals56362& periph) const
{
	// Load BootROM in DSP memory
	for (uint32_t i=0; i<bootRom.data.size(); i++)
		dsp.memory().set(dsp56k::MemArea_P, bootRom.offset + i, bootRom.data[i]);

//	dsp.memory().saveAssembly((m_file + "_BootROM.asm").c_str(), bootRom.offset, bootRom.size, false, false, &periph);

	// Attach command stream
	std::thread feedCommandStream([&]()
	{
		periph.getHDI08().writeRX(m_commandStream);
	});

	// Initialize the DSP
	dsp.setPC(bootRom.offset);
	return feedCommandStream;
}

bool ROMFile::getSingle(const int _bank, const int _presetNumber, TPreset& _out) const
{
	const uint32_t offset = 0x50000 + (_bank * 0x8000) + (_presetNumber * getSinglePresetSize());

	return getPreset(offset, _out);
}

bool ROMFile::getMulti(const int _presetNumber, TPreset& _out) const
{
	return getPreset(0x48000 + (_presetNumber * getMultiPresetSize()), _out);
}

bool ROMFile::getPreset(const uint32_t _offset, TPreset& _out) const
{
	if(_offset + getSinglePresetSize() > m_romFileData.size())
		return false;

	memcpy(_out.data(), &m_romFileData[_offset], getSinglePresetSize());
	return true;
}

std::string ROMFile::getSingleName(const TPreset& _preset)
{
	return getPresetName(_preset, 240, 249);
}

std::string ROMFile::getMultiName(const TPreset& _preset)
{
	return getPresetName(_preset, 4, 13);
}

std::string ROMFile::getPresetName(const TPreset& _preset, const uint32_t _first, const uint32_t _last)
{
	std::string name;

	name.reserve(11);

	for (uint32_t i = _first; i <= _last; i++)
	{
		const auto c = _preset[i];
		if(c < 32 || c > 127)
			break;
		name.push_back(static_cast<char>(c));
	}

	return name;
}

}
