#include "VirusPatchFileParser.h"

#include "virusLib/device.h"
#include "virusLib/import/TIControlStateDecoder.h"
#include "virusLib/microcontroller.h"
#include "virusLib/midiFileToRomData.h"

#include "synthLib/midiToSysex.h"
#include "synthLib/sounddiverLibLoader.h"

namespace genericVirusUI
{
	bool VirusPatchFileParser::parse(synthLib::SysexBufferList& _results, const synthLib::SysexBuffer& _data, const std::string& _filename)
	{
		const std::vector<uint8_t> dataVec(_data.begin(), _data.end());

		if(synthLib::SounddiverLibLoader::isValidData(dataVec))
		{
			synthLib::SounddiverLibLoader sd2s(dataVec);
			const auto& results = sd2s.getResults();

			if(!results.empty())
			{
				uint8_t prog = 0;
				for(const auto& res : results)
				{
					if(res.data.size() != 250)
						continue;

					auto& sysex = _results.emplace_back(synthLib::SysexBuffer
						{0xf0, 0x00, 0x20, 0x33, 0x01, virusLib::OMNI_DEVICE_ID, 0x10,
							static_cast<uint8_t>(prog >> 7), static_cast<uint8_t>(prog & 0x7f)});
					sysex.insert(sysex.end(), res.data.begin(), res.data.begin() + 240);
					for(size_t i = 0; i < 10; ++i)
						sysex.push_back(i < res.name.size() ? res.name[i] : ' ');
					sysex.insert(sysex.end(), res.data.begin() + 240, res.data.end() - 4);
					sysex.push_back(virusLib::Microcontroller::calcChecksum(sysex));
					sysex.push_back(0xf7);
					++prog;
				}
				return true;
			}
		}

		bool tiControlStateDecoded = false;
		{
			std::vector<synthLib::SMidiEvent> events;
			tiControlStateDecoded = virusLib::TIControlStateDecoder::decode(events, _data);
			if(tiControlStateDecoded)
			{
				for(const auto& event : events)
				{
					if(!event.sysex.empty())
						_results.push_back(event.sysex);
				}
			}
		}

		if(!tiControlStateDecoded)
		{
			if(virusLib::Device::parseVTIBackup(_results, _data))
				return true;

			bool parsed = virusLib::Device::parsePowercorePreset(_results, _data);
			parsed |= synthLib::MidiToSysex::extractSysexFromData(_results, _data);

			if(!parsed && !virusLib::Device::parseTDMPreset(_results, _data, _filename))
				return false;
		}

		if(!_results.empty() && _data.size() > 500000)
		{
			virusLib::MidiFileToRomData romLoader;
			for(const auto& result : _results)
			{
				if(!romLoader.add(result))
					break;
			}

			if(romLoader.isComplete())
			{
				const auto& data = romLoader.getData();
				if(data.size() > 0x10000)
				{
					constexpr ptrdiff_t startAddr = 0x10000;
					ptrdiff_t addr = startAddr;
					uint32_t index = 0;

					while(addr + 0x100 <= static_cast<ptrdiff_t>(data.size()))
					{
						const std::vector<uint8_t> chunk(data.begin() + addr, data.begin() + addr + 0x100);
						const auto idxL = chunk[3];
						if(idxL != (index & 0x7f))
							break;

						bool validName = true;
						for(size_t i = 240; i < 250; ++i)
						{
							if(chunk[i] < 32 || chunk[i] > 128)
							{
								validName = false;
								break;
							}
						}

						if(!validName)
							continue;

						addr += 0x100;
						++index;
					}

					if(index > 0)
					{
						_results.clear();
						for(uint32_t i = 0; i < index; ++i)
						{
							auto& sysex = _results.emplace_back(synthLib::SysexBuffer
								{0xf0, 0x00, 0x20, 0x33, 0x01, virusLib::OMNI_DEVICE_ID, 0x10,
									static_cast<uint8_t>(0x01 + (i >> 7)), static_cast<uint8_t>(i & 0x7f)});
							sysex.insert(sysex.end(), data.begin() + i * 0x100 + startAddr,
								data.begin() + i * 0x100 + 0x100 + startAddr);
							sysex.push_back(virusLib::Microcontroller::calcChecksum(sysex));
							sysex.push_back(0xf7);
						}
					}
				}
			}
		}

		mergeArrangements(_results);
		return !_results.empty();
	}

	void VirusPatchFileParser::mergeArrangements(synthLib::SysexBufferList& _results)
	{
		auto isMulti = [](const synthLib::SysexBuffer& _data)
		{
			return _data.size() >= 10 && _data[6] == virusLib::SysexMessageType::DUMP_MULTI;
		};
		auto isSingle = [](const synthLib::SysexBuffer& _data)
		{
			return _data.size() >= 10 && _data[6] == virusLib::SysexMessageType::DUMP_SINGLE;
		};

		synthLib::SysexBufferList merged;
		merged.reserve(_results.size());

		for(size_t i = 0; i < _results.size();)
		{
			if(isMulti(_results[i]) && i + 16 < _results.size())
			{
				bool allSingles = true;
				for(size_t j = 1; j <= 16; ++j)
				{
					if(!isSingle(_results[i + j]))
					{
						allSingles = false;
						break;
					}
				}

				if(allSingles)
				{
					auto compound = _results[i];
					for(size_t j = 1; j <= 16; ++j)
						compound.insert(compound.end(), _results[i + j].begin(), _results[i + j].end());
					merged.emplace_back(std::move(compound));
					i += 17;
					continue;
				}
			}

			merged.emplace_back(std::move(_results[i]));
			++i;
		}

		_results = std::move(merged);
	}
}
