#include "VirusParameterDescriptions.h"

#include "VirusParameter.h"
#include "VirusParameterDescription.h"

#include "dsp56kEmu/logging.h"

namespace Virus
{
	ParameterDescriptions::ParameterDescriptions(const std::string& _jsonString)
	{
		const auto err = loadJson(_jsonString);
		LOG(err);
	}

	std::string removeComments(std::string _json)
	{
		auto removeBlock = [&](const char* _begin, const char* _end)
		{
			const auto pos = _json.find(_begin);

			if (pos == std::string::npos)
				return false;

			const auto end = _json.find(_end, pos + 1);

			if (end != std::string::npos)
				_json.erase(pos, end - pos + 1);
			else
				_json.erase(pos);

			return true;
		};

		while (removeBlock("//", "\n") || removeBlock("/*", "*/"))
		{
		}

		return _json;
	}

	std::string ParameterDescriptions::loadJson(const std::string& _jsonString)
	{
		// juce' JSON parser doesn't like JSON5-style comments
		const auto jsonString = removeComments(_jsonString);

		juce::var json;

		const auto error = juce::JSON::parse(juce::String(jsonString), json);

		if (error.failed())
			return std::string("Failed to parse JSON: ") + std::string(error.getErrorMessage().toUTF8());

		const auto paramDescDefaults = json["parameterdescriptiondefaults"].getDynamicObject();
		const auto defaultProperties = paramDescDefaults ? paramDescDefaults->getProperties() : juce::NamedValueSet();
		const auto paramDescs = json["parameterdescriptions"];

		const auto descsArray = paramDescs.getArray();

		if (descsArray == nullptr)
			return "Parameter descriptions are empty";

		{
			const auto valueLists = json["valuelists"];

			auto* entries = valueLists.getDynamicObject();

			if (!entries)
				return "value lists are not defined";

			auto entryProps = entries->getProperties();

			for(int i=0; i<entryProps.size(); ++i)
			{
				const auto key = std::string(entryProps.getName(i).toString().toUTF8());
				const auto values = entryProps.getValueAt(i).getArray();

				if(m_valueLists.find(key) != m_valueLists.end())
					return "value list " + key + " is defined twice";

				if(!values || values->isEmpty())
					return std::string("value list ") + key + " is not a valid array of strings";

				ValueList vl;
				vl.texts.reserve(values->size());

				for (auto&& value : *values)
				{
					const auto text = static_cast<std::string>(value.toString().toUTF8());

					if (vl.textToValueMap.find(text) == vl.textToValueMap.end())
						vl.textToValueMap.insert(std::make_pair(text, static_cast<uint32_t>(vl.texts.size())));

					vl.texts.push_back(text);
				}

				m_valueLists.insert(std::make_pair(key, vl));
			}
		}

		std::stringstream errors;

		const auto& descs = *descsArray;

		for (int i = 0; i < descs.size(); ++i)
		{
			const auto& desc = descs[i].getDynamicObject();
			const auto props = desc->getProperties();

			if (props.isEmpty())
			{
				errors << "Parameter desc " << i << " has no properties defined" << std::endl;
				continue;
			}

			const auto name = props["name"].toString();

			if (name.isEmpty())
			{
				errors << "Parameter desc " << i << " doesn't have a name" << std::endl;
				continue;
			}

			auto readProperty = [&](const char* _key)
			{
				auto result = props[_key];
				if (!result.isVoid())
					return result;
				result = defaultProperties[_key];
				if (result.isVoid())
					errors << "Property " << _key << " not found for parameter description " << name << " and no default provided" << std::endl;
				return result;
			};

			auto readPropertyString = [&](const char* _key)
			{
				const auto res = readProperty(_key);

				if(!res.isString())
					errors << "Property " << _key << " of parameter desc " << name << " is not of type string" << std::endl;

				return std::string(res.toString().toUTF8());
			};

			auto readPropertyInt = [&](const char* _key)
			{
				const auto res = readProperty(_key);

				if (!res.isInt())
					errors << "Property " << _key << " of parameter desc " << name << " is not of type int " << std::endl;

				return static_cast<int>(res);
			};

			auto readPropertyBool = [&](const char* _key)
			{
				const auto res = readProperty(_key);

				if (res.isInt())
					return static_cast<int>(res) != 0;
				if (res.isBool())
					return static_cast<bool>(res);

				errors << "Property " << _key << " of parameter desc " << name << " is not of type bool " << std::endl;

				return static_cast<bool>(res);
			};

			const auto minValue = readPropertyInt("min");
			const auto maxValue = readPropertyInt("max");

			if (minValue < 0 || minValue > 127)
			{
				errors << name << ": min value for parameter desc " << name << " must be in range 0-127 but min is set to " << minValue << std::endl;
				continue;
			}
			if(maxValue < 0 || maxValue > 127)
			{
				errors << name << ": max value for parameter desc " << name << " must be in range 0-127 but max is set to " << maxValue << std::endl;
				continue;
			}
			if (maxValue < minValue)
			{
				errors << name << ": max value must be larger than min value but min is " << minValue << ", max is " << maxValue << std::endl;
				continue;
			}

			const auto valueList = readPropertyString("toText");

			const auto it = m_valueLists.find(valueList);
			if(it == m_valueLists.end())
			{
				errors << name << ": Value list " << valueList << " not found" << std::endl;
				continue;
			}

			const auto& list = *it;

			if((maxValue - minValue + 1) > static_cast<int>(list.second.texts.size()))
			{
				errors << name << ": value list " << valueList << " contains only " << list.second.texts.size() << " entries but parameter range is " << minValue << "-" << maxValue << std::endl;
			}

			Description d;

			d.name = name;

			d.isPublic = readPropertyBool("isPublic");
			d.isDiscrete = readPropertyBool("isDiscrete");
			d.isBool = readPropertyBool("isBool");
			d.isBipolar = readPropertyBool("isBipolar");

			d.toText = valueList;
			d.index = static_cast<uint8_t>(readPropertyInt("index"));

			d.range.setStart(minValue);
			d.range.setEnd(maxValue);

			d.valueList = it->second;

			{
				d.classFlags = 0;

				const auto classFlags = readPropertyString("class");

				std::vector<std::string> flags;
				size_t off = 0;

				while (true)
				{
					const auto pos = classFlags.find('|', off);

					if(pos == std::string::npos)
					{
						flags.push_back(classFlags.substr(off));
						break;
					}

					flags.push_back(classFlags.substr(off, pos - off));
					off = pos + 1;
				}

				for (const auto & flag : flags)
				{
					if (flag == "Undefined")
						d.classFlags |= Parameter::UNDEFINED;
					else if (flag == "Global")
						d.classFlags |= Parameter::GLOBAL;
					else if (flag == "PerformanceController")
						d.classFlags |= Parameter::PERFORMANCE_CONTROLLER;
					else if (flag == "SoundbankA")
						d.classFlags |= Parameter::SOUNDBANK_A;
					else if (flag == "SoundbankB")
						d.classFlags |= Parameter::SOUNDBANK_B;
					else if (flag == "MultiOrSingle")
						d.classFlags |= Parameter::MULTI_OR_SINGLE;
					else if (flag == "Multi")
						d.classFlags |= Parameter::MULTI_PARAM;
					else if (flag == "NonPartSensitive")
						d.classFlags |= Parameter::NON_PART_SENSITIVE;
					else if (flag == "BankProgramChangeParamBankSelect")
						d.classFlags |= Parameter::BANK_PROGRAM_CHANGE_PARAM_BANK_SELECT;
					else if (flag == "VirusC")
						d.classFlags |= Parameter::VIRUS_C;
					else
						errors << "Class " << flag << " is unknown" << std::endl;
				}
			}
			{
				const auto page = readPropertyString("page");

				if (page.size() != 1)
				{
					errors << name << ": Page " << page << " is an invalid value" << std::endl;
					continue;
				}

				switch (page[0])
				{
				case 'A':	d.page = virusLib::PAGE_A;	break;
				case 'B':	d.page = virusLib::PAGE_B;	break;
				case 'C':	d.page = virusLib::PAGE_C;	break;
				default:
					errors << name << ": Page " << page << " is an invalid value" << std::endl;
					break;
				}
			}

			m_descriptions.push_back(d);
		}

		return errors.str();
	}
}
