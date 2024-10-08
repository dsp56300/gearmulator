#include <fstream>
#include <algorithm>

#include "romfile.h"
#include "utils.h"

#include "unpacker.h"

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/logging.h"

#include "synthLib/os.h"

#include <cstring>	// memcpy

#include "demoplaybackTI.h"
#include "dspSingle.h"

namespace virusLib
{

ROMFile::ROMFile(std::vector<uint8_t> _data, std::string _name, const DeviceModel _model/* = DeviceModel::ABC*/) : m_model(_model), m_romFileName(std::move(_name)), m_romFileData(std::move(_data))
{
	if(initialize())
		return;
	m_romFileData.clear();
	m_bootRom.size = 0;
}

ROMFile ROMFile::invalid()
{
	return ROMFile({}, {}, DeviceModel::Invalid);
}

bool ROMFile::initialize()
{
	std::unique_ptr<std::istream> dsp(new imemstream(reinterpret_cast<std::vector<char>&>(m_romFileData)));

	ROMUnpacker::Firmware fw;

	// Check if we are dealing with a TI installer file, if so, unpack it first
	if (ROMUnpacker::isValidInstaller(*dsp))
	{
		fw = ROMUnpacker::getFirmware(*dsp, m_model);
		if (!fw.isValid())
		{
			LOG("Could not unpack ROM file");
			return false;
		}

		// Wrap into a stream so we can pass it into readChunks
		dsp.reset(new imemstream(fw.DSP));
	}

	const auto chunks = readChunks(*dsp);

	if (chunks.empty())
		return false;

	m_bootRom.size = chunks[0].items[0];
	m_bootRom.offset = chunks[0].items[1];
	m_bootRom.data = std::vector<uint32_t>(m_bootRom.size);

	// The first chunk contains the bootrom
	uint32_t i = 2;
	for (; i < m_bootRom.size + 2; i++)
	{
		m_bootRom.data[i-2] = chunks[0].items[i];
	}

	// The rest of the chunks is made up of the command stream
	for (size_t j = 0; j < chunks.size(); j++)
	{
		for (; i < chunks[j].items.size(); i++)
			m_commandStream.emplace_back(chunks[j].items[i]);
		i = 0;
	}

	printf("Program BootROM size = 0x%x\n", m_bootRom.size);
	printf("Program BootROM offset = 0x%x\n", m_bootRom.offset);
	printf("Program CommandStream size = 0x%x\n", static_cast<uint32_t>(m_commandStream.size()));

	if(isTIFamily())
	{
		if (!fw.Presets.empty())
		{
			for (auto& presetFile: fw.Presets)
			{
				imemstream stream(presetFile);
				loadPresetFile(stream, m_model);
			}

			for (const auto & preset : fw.Presets)
			{
				m_demoData.insert(m_demoData.begin(), preset.begin(), preset.end());
				if(DemoPlaybackTI::findDemoData(m_demoData))
					break;

				m_demoData.clear();
			}
		}
		else
		{
			loadPresetFiles();
		}

		// The Snow even has multis, but they are not sequencer compatible, drop them
		m_multis.clear();

		if(m_multis.empty())
		{
			// there is no multi in the TI presets, but there is an init multi in the F.bin

			const std::string search = "Init Multi";
			const auto searchSize = search.size();

			for(size_t i=0; i<fw.DSP.size() && m_multis.empty(); ++i)
			{
				for(size_t j=0; j<searchSize && m_multis.empty(); ++j)
				{
					if(fw.DSP[i+j] != search[j])
						break;

					if(j == searchSize-1)
					{
						TPreset preset;
						memcpy(preset.data(), &fw.DSP[i - 4], std::size(preset));

						// validate that we found the correct data by checking part volumes. It might just be a string somewhere in the data
						for(size_t k=0; k<16; ++k)
						{
							if(preset[k + 0x8b] != 0x40)
								break;

							if(k == 15)
							{
								for(size_t p=0; p<getPresetsPerBank(); ++p)
									m_multis.push_back(preset);
							}
						}
					}
				}
			}
		}

		// try to load the presets from the other roms, too
		auto loadFirmwarePresets = [this](const DeviceModel _model)
		{
			if(m_model == _model)
				return;
			const std::unique_ptr<imemstream> file(new imemstream(reinterpret_cast<std::vector<char>&>(m_romFileData)));
			const auto firmware = ROMUnpacker::getFirmware(*file, _model);
			if(!firmware.Presets.empty())
			{
				for (auto& presetFile: firmware.Presets)
				{
					imemstream stream(presetFile);
					loadPresetFile(stream, _model);
				}
			}
		};

		loadFirmwarePresets(DeviceModel::TI);
		loadFirmwarePresets(DeviceModel::TI2);
		loadFirmwarePresets(DeviceModel::Snow);
	}

	return true;
}

uint32_t ROMFile::getRomBankCount(const DeviceModel _model)
{
	switch (_model)
	{
	case DeviceModel::TI:
		return 26 - 7;
	case DeviceModel::Snow:
		return 8;
	case DeviceModel::TI2:
		return 26 - 5 + 4;
	default:
		return 8;
	}
}

std::vector<ROMFile::Chunk> ROMFile::readChunks(std::istream& _file) const
{
	_file.seekg(0, std::ios_base::end);
	const auto fileSize = _file.tellg();

	uint32_t offset;
	int lastChunkId;

	if(fileSize == getRomSizeModelD())
	{
		assert(isTIFamily());
		offset = 0x70000;
		lastChunkId = 14;
	}
	else if (fileSize == getRomSizeModelABC() || fileSize == getRomSizeModelABC()/2)	// the latter is a ROM without presets
	{
		// ABC
		assert(isABCFamily(m_model));
		offset = 0x18000;
		lastChunkId = 4;
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

		if(i == 0 && chunk.chunk_id == 3 && lastChunkId == 4)	// Virus A and old Virus B OSs have one chunk less
			lastChunkId = 3;

		if(chunk.chunk_id != lastChunkId - i)
			return {};

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

bool ROMFile::loadPresetFiles()
{
	bool res = true;
	for (auto &filename: {"S.bin", "P.bin"})
	{
		std::ifstream file(filename, std::ios::binary | std::ios::ate);
		if (!file.is_open())
		{
			LOG("Failed to open preset file " << filename);
			res = false;
			continue;
		}
		res &= loadPresetFile(file, m_model);
		file.close();
	}
	return res;
}

bool ROMFile::loadPresetFile(std::istream& _file, DeviceModel _model)
{
	_file.seekg(0, std::ios_base::end);
	const auto fileSize = _file.tellg();

	uint32_t singleCount = 0;
	uint32_t multiCount = 0;
	uint32_t multiOffset = 0;

	if (fileSize == 0x1b0000)		// TI/TI2
	{
		if(_model == DeviceModel::TI2)
			singleCount = 128 * (26 - 5);
		else
			singleCount = 128 * (26 - 7);
	}
	else if (fileSize == 0x40000)	// Snow A
	{
		singleCount = 512;
	}
	else if (fileSize == 0x68000)	// Snow B
	{
		singleCount = 512;
		multiCount = 128;
		multiOffset = 768;
	}
	else
	{
		LOG("Unknown file size " << fileSize << " for preset file");
		return false;
	}

	_file.seekg(0);

	for(uint32_t i=0; i<singleCount; ++i)
	{
		TPreset single;
		_file.read(reinterpret_cast<char*>(&single), sizeof(single));
		m_singles.emplace_back(single);

		LOG("Loaded single " << i << ", name = " << getSingleName(single));
	}

	if(multiCount)
	{
		const auto off = std::max(singleCount, multiOffset);
		_file.seekg(off * 512);

		for (uint32_t i = 0; i < multiCount; ++i)
		{
			TPreset multi;
			_file.read(reinterpret_cast<char*>(&multi), sizeof(multi));
			m_multis.emplace_back(multi);

			LOG("Loaded multi " << i << ", name = " << getMultiName(multi));
		}
	}

	return true;
}

std::thread ROMFile::bootDSP(DspSingle& _dsp) const
{
	return _dsp.boot(m_bootRom, m_commandStream);
}

std::string ROMFile::getModelName() const
{
	return virusLib::getModelName(getModel());
}

bool ROMFile::getSingle(const int _bank, const int _presetNumber, TPreset& _out) const
{
	if(isTIFamily())
	{
		const auto offset = _bank * getSinglesPerBank() + _presetNumber;
		if (offset >= m_singles.size())
			return false;
		_out = m_singles[offset];
		return true;
	}

	const uint32_t offset = 0x50000 + (_bank * 0x8000) + (_presetNumber * getSinglePresetSize());

	return getPreset(offset, _out);
}

bool ROMFile::getMulti(const int _presetNumber, TPreset& _out) const
{
	if(isTIFamily())
	{
		if (_presetNumber >= m_multis.size())
			return false;

		_out = m_multis[_presetNumber];
		return true;
	}

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
