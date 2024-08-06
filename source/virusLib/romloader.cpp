#include "romloader.h"

#include <algorithm>

#include "midiFileToRomData.h"

#include "synthLib/os.h"

namespace virusLib
{
	static constexpr uint32_t g_midiSizeMinABC = 500 * 1024;
	static constexpr uint32_t g_midiSizeMaxABC = 600 * 1024;

	static constexpr uint32_t g_binSizeTImin = 6 * 1024 * 1024;
	static constexpr uint32_t g_binSizeTImax = 9 * 1024 * 1024;

	std::vector<ROMFile> ROMLoader::findROMs(const DeviceModel _model/* = DeviceModel::ABC*/)
	{
		return findROMs(std::string(), _model);
	}

	std::vector<ROMFile> ROMLoader::findROMs(const DeviceModel _modelA, const DeviceModel _modelB)
	{
		auto roms = findROMs(_modelA);
		const auto romsB = findROMs(_modelB);
		if(!romsB.empty())
			roms.insert(roms.end(), romsB.begin(), romsB.end());

		return roms;
	}

	std::vector<ROMFile> ROMLoader::findROMs(const std::string& _path, const DeviceModel _model /*= DeviceModel::ABC*/)
	{
		std::vector<std::string> files;

		if(isABCFamily(_model))
		{
			files = findFiles(_path, ".bin", ROMFile::getRomSizeModelABC(), ROMFile::getRomSizeModelABC());

			const auto midiFileNames = findFiles(_path, ".mid", g_midiSizeMinABC, g_midiSizeMaxABC);

			files.insert(files.end(), midiFileNames.begin(), midiFileNames.end());
		}
		else
		{
			files = findFiles(_path, ".bin", g_binSizeTImin, g_binSizeTImax);
		}

		return initializeRoms(files, _model);
	}

	ROMFile ROMLoader::findROM(const DeviceModel _model/* = DeviceModel::ABC*/)
	{
		std::vector<ROMFile> results = findROMs(_model);
		return results.empty() ? ROMFile::invalid() : results.front();
	}

	ROMFile ROMLoader::findROM(const std::string& _filename, const DeviceModel _model/* = DeviceModel::ABC*/)
	{
		if(_filename.empty())
			return findROM(_model);

		const auto path = synthLib::getPath(_filename);

		const std::vector<ROMFile> results = findROMs(path, _model);

		if(results.empty())
			return ROMFile::invalid();

		const auto requestedName = synthLib::lowercase(_filename);

		for (const auto& result : results)
		{
			const auto name = synthLib::lowercase(result.getFilename());
			if(name == requestedName)
				return result;
			if(synthLib::getFilenameWithoutPath(name) == requestedName)
				return result;
		}
		return ROMFile::invalid();
	}

	ROMLoader::FileData ROMLoader::loadFile(const std::string& _name)
	{
		FileData data;
		data.filename = _name;

		if(!synthLib::readFile(data.data, _name))
			return {};

		if(synthLib::hasExtension(_name, ".bin"))
		{
			data.type = BinaryRom;
			return data;
		}

		if(!synthLib::hasExtension(_name, ".mid"))
			return {};

		MidiFileToRomData midiLoader;
		if(!midiLoader.load(data.data, true))
			return {};

		data.data = midiLoader.getData();

		if(data.data.size() != (ROMFile::getRomSizeModelABC()>>1))
		{
			if(data.data.size() == 0x38000)
			{
				// Virus A midi OS update has $2000 less than all others
				data.data.resize(ROMFile::getRomSizeModelABC()>>1, 0xff);
			}
			else
			{
				return {};
			}
		}

		if(midiLoader.getFirstSector() == 0)
			data.type = MidiRom;
		else if(midiLoader.getFirstSector() == 8)
			data.type = MidiPresets;
		else
			return {};
		return data;
	}

	DeviceModel ROMLoader::detectModel(const std::vector<uint8_t>& _data)
	{
		// examples
		// A: (C)ACCESS [08-20-2001-16:58:54][v280g]
		// B: (C)ACCESS [12-23-2003-14:43:27][VB_490T]
		// C: (C)ACCESS [11-10-2003-12:15:42][vc_650b]

		const std::string key = "(C)ACCESS [";
		const auto result = std::search(_data.begin(), _data.end(), std::begin(key), std::end(key));
		if(result == _data.end())
			return DeviceModel::Invalid;

		const auto bracketOpen = std::find(result+static_cast<int32_t>(key.size())+1, _data.end(), '[');
		if(bracketOpen == _data.end())
			return DeviceModel::Invalid;

		const auto bracketClose = std::find(bracketOpen+1, _data.end(), ']');
		if(bracketClose == _data.end())
			return DeviceModel::Invalid;

		const auto versionString = synthLib::lowercase(std::string(bracketOpen+1, bracketClose-1));

		const auto test = [&versionString](const char* _key)
		{
			return versionString.find(_key) == 0;
		};

		if(test("vb") || test("vcl") || test("vrt"))
			return DeviceModel::B;

		if(test("vc") || test("vr_6"))
			return DeviceModel::C;

		if(test("vr"))
			return DeviceModel::B;

		if(test("v2"))
			return DeviceModel::A;

		return DeviceModel::Invalid;
	}

	std::vector<ROMFile> ROMLoader::initializeRoms(const std::vector<std::string>& _files, const DeviceModel _model)
	{
		if(_files.empty())
			return {};

		std::vector<FileData> fileDatas;
		fileDatas.reserve(_files.size());

		for (const auto& file : _files)
		{
			auto data = loadFile(file);
			if(!data.isValid())
				continue;

			fileDatas.emplace_back(std::move(data));
		}

		if(fileDatas.empty())
			return {};

		std::vector<ROMFile> roms;
		roms.reserve(fileDatas.size());

		std::vector<const FileData*> presets;

		for (const auto& fd : fileDatas)
		{
			if(fd.type == MidiPresets)
				presets.push_back(&fd);
		}

		for (const auto& fd : fileDatas)
		{
			if(fd.type == MidiPresets)
				continue;

			DeviceModel model;

			if(isTIFamily(_model))
			{
				// model is specified externally as firmware has every model inside
				model = _model;
			}
			else
			{
				model = detectModel(fd.data);

				if(model == DeviceModel::Invalid)
				{
					// disable load if model is not detected, because we now have other synths that have roms of the same size
//					assert(false && "retry model detection for debugging purposes below");
					detectModel(fd.data);
					continue;
//					model = _model;	// Must be based on DSP 56362 or hell breaks loose
				}
			}

			if(fd.type == BinaryRom)
			{
				// load as-is
				auto& rom = roms.emplace_back(fd.data, fd.filename, model);
				if(!rom.isValid())
					roms.pop_back();
			}
			else if(fd.type == MidiRom)
			{
				// try to combine with presets
				if(presets.empty())
				{
					// none available, use without presets
					auto& rom = roms.emplace_back(fd.data, fd.filename, model);
					if(!rom.isValid())
						roms.pop_back();
				}
				else
				{
					// build data with presets included and create rom
					auto& p = *presets.front();
					auto data = fd.data;
					data.insert(data.end(), p.data.begin(), p.data.end());

					auto& rom = roms.emplace_back(data, fd.filename, model);
					if(!rom.isValid())
						roms.pop_back();
					else if(presets.size() > 1)
						presets.erase(presets.begin());	// do not use preset file more than once if we have multiple
				}
			}
		}

		return roms;
	}
}
