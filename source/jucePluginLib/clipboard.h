#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "types.h"

namespace pluginLib
{
	class Processor;

	class Clipboard
	{
	public:
		struct Data
		{
			using ParameterValues = std::map<std::string, ParamValue>;

			std::string pluginName;
			std::string pluginVersionString;
			uint32_t pluginVersionNumber;
			uint32_t formatVersion;

			std::string parameterRegion;

			ParameterValues parameterValues;
			std::map<std::string,ParameterValues> parameterValuesByRegion;

			std::vector<std::vector<uint8_t>> sysex;

			bool empty() const
			{
				return sysex.empty() && parameterValues.empty();
			}
		};

		static std::string midiDataToString(const std::vector<uint8_t>& _data, uint32_t _bytesPerLine = 32);
		static std::vector<std::vector<uint8_t>> getSysexFromString(const std::string& _text);
		static std::string parametersToString(Processor& _processor, const std::vector<std::string>& _parameters, const std::string& _regionId);
		static std::string createJsonString(Processor& _processor, const std::vector<std::string>& _parameters, const std::string& _regionId, const std::vector<uint8_t>& _sysex);
		static Data getDataFromString(Processor& _processor, const std::string& _text);
	};
}
