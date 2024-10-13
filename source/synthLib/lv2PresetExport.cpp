#include "lv2PresetExport.h"

#include <fstream>
#include <map>

#include "os.h"

namespace synthLib
{
	namespace
	{
		const std::string g_keyBankName = "%BANKNAME%";
		const std::string g_keyBankId = "%BANKID%";
		const std::string g_keyPluginId = "%PLUGINID%";
		const std::string g_keyPresetFilename = "%PRESETFILENAME%";
		const std::string g_keyPresetName = "%PRESETNAME%";
		const std::string g_keyPresetData = "%PRESETDATA%";

		const std::string g_manifestHeader =
			"@prefix atom: <http://lv2plug.in/ns/ext/atom#> .\n"
			"@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n"
			"@prefix pset: <http://lv2plug.in/ns/ext/presets#> .\n"
			"@prefix rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .\n"
			"@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n"
			"@prefix state: <http://lv2plug.in/ns/ext/state#> .\n"
			"@prefix xsd: <http://www.w3.org/2001/XMLSchema#> .\n"
			"\n";

		const std::string g_manifestBankEntry =
			"<" + g_keyBankId + ">\n"
			"	lv2:appliesTo <" + g_keyPluginId + "> ;\n"
			"	a pset:Bank ;\n"
			"	rdfs:label \"" + g_keyBankName + "\" .\n"
			"\n";

		const std::string g_manifestPresetEntry =
		"<" + g_keyPresetFilename + ">\n"
		"	lv2:appliesTo <" + g_keyPluginId + "> ;\n"
		"	a pset:Preset ;\n"
		"	pset:bank <" + g_keyBankId + "> ;\n"
		"	rdfs:seeAlso <" + g_keyPresetFilename + "> .\n"
		"\n";

		const std::string g_presetFile = 
			"@prefix atom: <http://lv2plug.in/ns/ext/atom#> .\n"
			"@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n"
			"@prefix pset: <http://lv2plug.in/ns/ext/presets#> .\n"
			"@prefix rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .\n"
			"@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n"
			"@prefix state: <http://lv2plug.in/ns/ext/state#> .\n"
			"@prefix xsd: <http://www.w3.org/2001/XMLSchema#> .\n"
			"\n"
			"<>\n"
			"	a pset:Preset ;\n"
			"	lv2:appliesTo <" + g_keyPluginId + "> ;\n"
			"	rdfs:label \"" + g_keyPresetName + "\" ;\n"
			"	state:state [\n"
			"		<" + g_keyPluginId + ":StateString> \"" + g_keyPresetData + "\"\n"
			"	] .\n"
			"\n";

		std::string toFilename(std::string _name)
		{
			for (char& c : _name)
			{
				if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
					continue;
				c = '_';
			}
			return _name;
		}

		std::string replaceVariables(std::string _string, const std::map<std::string, std::string>& _variables)
		{
			for (const auto& [key, value] : _variables)
			{
				while(true)
				{
					const auto pos = _string.find(key);
					if(pos == std::string::npos)
						break;
					_string.erase(pos, key.size());
					_string.insert(pos, value);
				}
			}
			return _string;
		}

		int getBitRange (const std::vector<uint8_t>& _data, size_t bitRangeStart, size_t numBits)
		{
		    int res = 0;

		    auto byte = bitRangeStart >> 3;
		    auto offsetInByte = bitRangeStart & 7;
		    size_t bitsSoFar = 0;

		    while (numBits > 0 && (size_t) byte < _data.size())
		    {
			    const auto bitsThisTime = std::min(numBits, 8 - offsetInByte);
		        const int mask = (0xff >> (8 - bitsThisTime)) << offsetInByte;

		        res |= (((_data[byte] & mask) >> offsetInByte) << bitsSoFar);

		        bitsSoFar += bitsThisTime;
		        numBits -= bitsThisTime;
		        ++byte;
		        offsetInByte = 0;
		    }

		    return res;
		}

		constexpr char base64EncodingTable[] = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+";

		std::string dataToJuceEncodedString(const std::vector<uint8_t>& _data)
		{
		    const auto numChars = ((_data.size() << 3) + 5) / 6;

			// store the length, followed by a '.', and then the data
		    std::string destString = std::to_string(numChars);
			destString.reserve(_data.size());

		    const auto initialLen = destString.size();
		    destString.reserve(initialLen + 2 + numChars);

			destString.push_back('.');

			for (size_t i = 0; i < numChars; ++i)
				destString.push_back(base64EncodingTable[getBitRange (_data, i * 6, 6)]);

			return destString;
		}
	}

	bool Lv2PresetExport::exportPresets(const std::string& _outputPath, const std::string& _pluginId, const std::vector<Bank>& _banks)
	{
		const auto path = validatePath(_outputPath);
		createDirectory(path);

		for (const auto& bank : _banks)
		{
			if(!exportPresets(getBankPath(path, bank.name), _pluginId, bank))
				return false;
		}
		return true;
	}

	bool Lv2PresetExport::exportPresets(const std::string& _outputPath, const std::string& _pluginId, const Bank& _bank)
	{
		const auto path = validatePath(_outputPath);
		createDirectory(path);
		std::ofstream manifest(getManifestFilename(path));
		if(!manifest.is_open())
			return false;

		std::map<std::string, std::string> bankVars;

		bankVars.insert({g_keyPluginId, _pluginId});
		bankVars.insert({g_keyBankId, toFilename(_bank.name)});
		bankVars.insert({g_keyBankName, _bank.name});

		manifest << replaceVariables(g_manifestHeader, bankVars);
		manifest << replaceVariables(g_manifestBankEntry, bankVars);

		for (const auto& preset : _bank.presets)
		{
			const auto presetFilename = toFilename(preset.name) + ".ttl";

			auto presetVars = bankVars;

			presetVars.insert({g_keyPresetName, preset.name});
			presetVars.insert({g_keyPresetFilename, presetFilename});
			presetVars.insert({g_keyPresetData, dataToJuceEncodedString(preset.data)});

			std::ofstream presetFile(path + presetFilename);

			if(!presetFile.is_open())
				return false;

			manifest << replaceVariables(g_manifestPresetEntry, presetVars);

			presetFile << replaceVariables(g_presetFile, presetVars);

			presetFile.close();
		}

		return true;
	}

	std::string Lv2PresetExport::getBankPath(const std::string& _outputPath, const std::string& _bankName)
	{
		return validatePath(_outputPath) + getBankFilename(_bankName) + ".lv2/";
	}

	std::string Lv2PresetExport::getManifestFilename(const std::string& _path)
	{
		return validatePath(_path) + "manifest.ttl";
	}

	std::string Lv2PresetExport::getBankFilename(const std::string& _bankName)
	{
		return toFilename(_bankName);
	}

	bool Lv2PresetExport::manifestFileExists(const std::string& _path)
	{
		const auto manifestFile = getManifestFilename(_path);

		FILE* const hFile = fopen(manifestFile.c_str(), "rb");
		if(!hFile)
			return false;
		(void)fclose(hFile);
		return true;
	}
}
