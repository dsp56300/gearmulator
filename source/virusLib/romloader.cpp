#include "romloader.h"

#include "midiFileToRomData.h"

#include "../synthLib/os.h"

namespace virusLib
{
	static constexpr uint32_t g_midiSizeMinABC = 512 * 1024;
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

	std::vector<std::string> ROMLoader::findFiles(const std::string& _extension, const size_t _minSize, const size_t _maxSize)
	{
		std::vector<std::string> results;

        const auto path = synthLib::getModulePath();
		synthLib::findFiles(results, path, _extension, _minSize, _maxSize);

    	const auto path2 = synthLib::getModulePath(false);
		if(path2 != path)
			synthLib::findFiles(results, path2, _extension, _minSize, _maxSize);

		if(results.empty())
		{
            const auto path3 = synthLib::getCurrentDirectory();
			if(path3 != path2 && path3 != path)
				synthLib::findFiles(results, path, _extension, _minSize, _maxSize);
		}

		return results;
	}

	std::vector<std::string> ROMLoader::findFiles(const std::string& _path, const std::string& _extension, size_t _minSize, size_t _maxSize)
	{
		if(_path.empty())
			return findFiles(_extension, _minSize, _maxSize);

		std::vector<std::string> results;
		synthLib::findFiles(results, _path, _extension, _minSize, _maxSize);
		return results;
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
			return {};

		if(midiLoader.getFirstSector() == 0)
			data.type = MidiRom;
		else if(midiLoader.getFirstSector() == 8)
			data.type = MidiPresets;
		else
			return {};
		return data;
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

			if(fd.type == BinaryRom)
			{
				// load as-is
				auto& rom = roms.emplace_back(fd.data, fd.filename, _model);
				if(!rom.isValid())
					roms.pop_back();
			}
			else if(fd.type == MidiRom)
			{
				// try to combine with presets
				if(presets.empty())
				{
					// none available, use without presets
					auto& rom = roms.emplace_back(fd.data, fd.filename, _model);
					if(!rom.isValid())
						roms.pop_back();
				}
				else
				{
					// build data with presets included and create rom
					auto& p = *presets.front();
					auto data = fd.data;
					data.insert(data.end(), p.data.begin(), p.data.end());

					auto& rom = roms.emplace_back(data, fd.filename, _model);
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
