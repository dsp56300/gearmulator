#include "clipboard.h"

#include "../synthLib/midiToSysex.h"
#include "../synthLib/os.h"

#include <sstream>

#include "dsp56kEmu/logging.h"

namespace pluginLib
{
	std::string Clipboard::midiDataToString(const std::vector<uint8_t>& _data, const uint32_t _bytesPerLine/* = 32*/)
	{
		if(_data.empty())
			return {};

		std::stringstream ss;

		for(size_t i=0; i<_data.size();)
		{
			if(i)
				ss << '\n';
			for(size_t j=0; j<_bytesPerLine && i<_data.size(); ++j, ++i)
			{
				if(j)
					ss << ' ';
				ss << HEXN(static_cast<uint32_t>(_data[i]), 2);
			}
		}

		return ss.str();
	}

	std::vector<std::vector<uint8_t>> Clipboard::getSysexFromString(const std::string& _text)
	{
		if(_text.empty())
			return {};

		auto text = synthLib::lowercase(_text);

		while(true)
		{
			const auto pos = text.find_first_of(" \n\r\t");
			if(pos == std::string::npos)
				break;
			text = text.substr(0,pos) + text.substr(pos+1);
		}

		const auto posF0 = text.find("f0");
		if(posF0 == std::string::npos)
			return {};

		const auto posF7 = text.rfind("f7");
		if(posF7 == std::string::npos)
			return {};

		if(posF7 <= posF0)
			return {};

		const auto dataString = text.substr(posF0, posF7 + 2 - posF0);

		if(dataString.size() & 1)
			return {};

		std::vector<uint8_t> data;
		data.reserve(dataString.size()>>1);

		for(size_t i=0; i<dataString.size(); i+=2)
		{
			char temp[3]{0,0,0};
			temp[0] = dataString[i];
			temp[1] = dataString[i+1];

			const auto c = strtoul(temp, nullptr, 16);
			if(c < 0 || c > 255)
				return {};
			data.push_back(static_cast<uint8_t>(c));
		}

		std::vector<std::vector<uint8_t>> results;
		synthLib::MidiToSysex::extractSysexFromData(results, data);

		return results;
	}
}
