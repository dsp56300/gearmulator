#include "clipboard.h"

#include "synthLib/midiToSysex.h"

#include <sstream>

#include "pluginVersion.h"
#include "processor.h"

#include "baseLib/filesystem.h"

#include "dsp56kEmu/logging.h"

#include "juce_core/juce_core.h"

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

		auto text = baseLib::filesystem::lowercase(_text);

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

	std::string Clipboard::parametersToString(Processor& _processor, const std::vector<std::string>& _parameters, const std::string& _regionId)
	{
		if(_parameters.empty())
			return {};

		return createJsonString(_processor, _parameters, _regionId, {});
	}

	std::string Clipboard::createJsonString(Processor& _processor, const std::vector<std::string>& _parameters, const std::string& _regionId, const std::vector<uint8_t>& _sysex)
	{
		if(_parameters.empty() && _sysex.empty())
			return {};

		const auto json = juce::ReferenceCountedObjectPtr<juce::DynamicObject>(new juce::DynamicObject());

		json->setProperty("plugin", juce::String(_processor.getProperties().name));
		json->setProperty("pluginVersion", juce::String(Version::getVersionString()));
		json->setProperty("pluginVersionNumber", juce::String(Version::getVersionNumber()));

		json->setProperty("formatVersion", 1);

		if(!_regionId.empty())
			json->setProperty("region", juce::String(_regionId));

		const auto& c = _processor.getController();
		const auto part = c.getCurrentPart();

		if(!_parameters.empty())
		{
			const auto params = juce::ReferenceCountedObjectPtr<juce::DynamicObject>(new juce::DynamicObject());

			for (const auto& param : _parameters)
			{
				const auto* p = c.getParameter(param, part);

				if(!p)
					continue;

				params->setProperty(juce::String(p->getDescription().name), p->getUnnormalizedValue());
			}

			json->setProperty("parameters", params.get());
		}

		if(!_sysex.empty())
			json->setProperty("sysex", juce::String(midiDataToString(_sysex, static_cast<uint32_t>(_sysex.size()))));

		const auto result = juce::JSON::toString(json.get());

		return result.toStdString();
	}

	Clipboard::Data Clipboard::getDataFromString(Processor& _processor, const std::string& _text)
	{
		if(_text.empty())
			return {};

		const auto json = juce::JSON::parse(juce::String(_text));

		Data data;

		auto parseRawMidi = [&]()
		{
			data.sysex = getSysexFromString(_text);
			return data;
		};

		data.pluginName = json["plugin"].toString().toStdString();

		// for this plugin or another plugin?
		if(data.pluginName != _processor.getProperties().name)
			return parseRawMidi();

		data.pluginVersionNumber = static_cast<int>(json["pluginVersionNumber"]);

		// version cannot be lower than when we first added the feature
		if(data.pluginVersionNumber < 10315)
			return parseRawMidi();

		data.formatVersion = static_cast<int>(json["formatVersion"]);

		// we only support version 1 atm
		if(data.formatVersion != 1)
			return parseRawMidi();

		data.pluginVersionString = json["pluginVersion"].toString().toStdString();

		data.parameterRegion = json["region"].toString().toStdString();

		const auto* params = json["parameters"].getDynamicObject();

		if(params)
		{
			const auto& c = _processor.getController();

			const auto& props = params->getProperties();
			for (const auto& it : props)
			{
				const auto name = it.name.toString().toStdString();
				const int value = it.value;

				// something is very wrong if a parameter exists twice
				if(data.parameterValues.find(name) != data.parameterValues.end())
					return parseRawMidi();

				// also if a parameter value is out of range
				if(value < 0 || value > 127)
					return parseRawMidi();

				// gracefully ignore parameters that we are not aware of. Parameters might change in the future or whatever
				if(c.getParameterIndexByName(name) == Controller::InvalidParameterIndex)
					continue;

				data.parameterValues.insert(std::make_pair(name, static_cast<uint8_t>(value)));
			}

			if(!data.parameterValues.empty())
			{
				for (const auto& it : data.parameterValues)
				{
					const auto& paramName = it.first;

					const auto regionIds = c.getRegionIdsForParameter(paramName);

					for (const auto& regionId : regionIds)
						data.parameterValuesByRegion[regionId].insert(it);
				}
			}
		}

		const auto sysex = json["sysex"].toString().toStdString();

		if(!sysex.empty())
			data.sysex = getSysexFromString(sysex);

		return data;
	}
}
