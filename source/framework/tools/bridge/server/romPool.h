#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <vector>
#include <string>

namespace baseLib
{
	class MD5;
}

namespace bridgeServer
{
	struct Config;
	using RomData = std::vector<uint8_t>;

	class RomPool
	{
	public:
		RomPool(Config& _config);

		const RomData& getRom(const baseLib::MD5& _hash);
		void addRom(const std::string& _name, const RomData& _data);

	private:
		std::string getRootPath() const;
		void findRoms();

		const Config& m_config;

		std::mutex m_mutex;
		std::map<baseLib::MD5, RomData> m_roms;
	};
}
