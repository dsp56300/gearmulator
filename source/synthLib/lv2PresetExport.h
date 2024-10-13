#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace synthLib
{
	class Lv2PresetExport
	{
	public:
		using PresetData = std::vector<uint8_t>;

		struct Preset
		{
			std::string name;
			PresetData data;
		};

		struct Bank
		{
			std::string name;
			std::vector<Preset> presets;
		};

		static bool exportPresets(const std::string& _outputPath, const std::string& _pluginId, const std::vector<Bank>& _banks);
		static bool exportPresets(const std::string& _outputPath, const std::string& _pluginId, const Bank& _bank);

		static std::string getBankPath(const std::string& _outputPath, const std::string& _bankName);
		static std::string getManifestFilename(const std::string& _path);

		static std::string getBankFilename(const std::string& _bankName);
		static bool manifestFileExists(const std::string& _path);
	};
}
