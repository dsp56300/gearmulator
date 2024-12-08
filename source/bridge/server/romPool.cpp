#include "romPool.h"

#include "config.h"
#include "baseLib/md5.h"
#include "networkLib/logging.h"
#include "synthLib/os.h"

namespace bridgeServer
{
	RomPool::RomPool(Config& _config) : m_config(_config)
	{
		findRoms();
	}

	const RomData& RomPool::getRom(const baseLib::MD5& _hash)
	{
		std::scoped_lock lock(m_mutex);

		auto it = m_roms.find(_hash);
		if(it == m_roms.end())
			findRoms();
		it = m_roms.find(_hash);
		if(it != m_roms.end())
			return it->second;
		static RomData empty;
		return empty;
	}

	void RomPool::addRom(const std::string& _name, const RomData& _data)
	{
		std::scoped_lock lock(m_mutex);

		const auto hash = baseLib::MD5(_data);
		if(m_roms.find(hash) != m_roms.end())
			return;

		if(synthLib::writeFile(getRootPath() + _name + '_' + hash.toString() + ".bin", _data))
			m_roms.insert({hash, _data});
	}

	std::string RomPool::getRootPath() const
	{
		return m_config.romsPath;
	}

	void RomPool::findRoms()
	{
		std::vector<std::string> files;
		synthLib::findFiles(files, getRootPath(), {}, 0, 16 * 1024 * 1024);

		for (const auto& file : files)
		{
			std::vector<uint8_t> romData;

			if(!synthLib::readFile(romData, file))
			{
				LOGNET(networkLib::LogLevel::Error, "Failed to load file " << file);
				continue;
			}

			const auto hash = baseLib::MD5(romData);

			if(m_roms.find(hash) != m_roms.end())
				continue;

			m_roms.insert({hash, std::move(romData)});
			LOGNET(networkLib::LogLevel::Info, "Loaded ROM " << synthLib::getFilenameWithoutPath(file));
		}
	}
}
