#include "parameterdescriptions.h"

#include <cassert>

#include "dsp56kEmu/logging.h"

#include "synthLib/midiTypes.h"

namespace pluginLib
{
	ParameterDescriptions::ParameterDescriptions(const std::string& _jsonString)
	{
		m_errors = loadJson(_jsonString);
	}

	const MidiPacket* ParameterDescriptions::getMidiPacket(const std::string& _name) const
	{
		const auto it = m_midiPackets.find(_name);
		return it == m_midiPackets.end() ? nullptr : &it->second;
	}

	std::string ParameterDescriptions::removeComments(std::string _json)
	{
		auto removeBlock = [&](const std::string& _begin, const std::string& _end)
		{
			const auto pos = _json.find(_begin);

			if (pos == std::string::npos)
				return false;

			const auto end = _json.find(_end, pos + 1);

			if (end != std::string::npos)
				_json.erase(pos, end - pos + _end.size());
			else
				_json.erase(pos);

			return true;
		};

		while (removeBlock("//", "\n") || removeBlock("/*", "*/"))
		{
		}

		return _json;
	}

	bool ParameterDescriptions::getIndexByName(uint32_t& _index, const std::string& _name) const
	{
		const auto it = m_nameToIndex.find(_name);
		if(it == m_nameToIndex.end())
			return false;
		_index = it->second;
		return true;
	}

	const ValueList* ParameterDescriptions::getValueList(const std::string& _key) const
	{
		const auto it = m_valueLists.find(_key);
		if(it == m_valueLists.end())
			return nullptr;
		return &it->second;
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

		std::stringstream errors;

		{
			const auto& valueLists = json["valuelists"];

			auto* entries = valueLists.getDynamicObject();

			if (!entries)
				return "value lists are not defined";

			auto entryProps = entries->getProperties();

			for(int i=0; i<entryProps.size(); ++i)
			{
				const auto key = std::string(entryProps.getName(i).toString().toUTF8());
				const auto& values = entryProps.getValueAt(i);

				const auto result = parseValueList(key, values);

				if(!result.empty())
					errors << "Failed to parse value list " << key << ": " << result << '\n';
			}
		}

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
			const auto displayName = props["displayName"].toString().toStdString();

			if (name.isEmpty())
			{
				errors << "Parameter desc " << i << " doesn't have a name" << std::endl;
				continue;
			}

			auto readProperty = [&](const char* _key, bool _faiIfNotExisting = true)
			{
				auto result = props[_key];
				if (!result.isVoid())
					return result;
				result = defaultProperties[_key];
				if (_faiIfNotExisting && result.isVoid())
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

			auto readPropertyIntWithDefault = [&](const char* _key, const int _defaultValue)
			{
				const auto res = readProperty(_key, false);

				if (!res.isInt())
					return _defaultValue;

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
			const auto defaultValue = readPropertyIntWithDefault("default", Description::NoDefaultValue);

			if (maxValue < minValue)
			{
				errors << name << ": max value must be larger than min value but min is " << minValue << ", max is " << maxValue << std::endl;
				continue;
			}

			if(defaultValue != Description::NoDefaultValue && (defaultValue < minValue || defaultValue > maxValue))
			{
				errors << name << ": default value must be within parameter range " << minValue << " to " << maxValue << " but default value is specified as " << defaultValue << std::endl;
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

			d.name = name.toStdString();
			d.displayName = displayName.empty() ? d.name : displayName;

			d.isPublic = readPropertyBool("isPublic");
			d.isDiscrete = readPropertyBool("isDiscrete");
			d.isBool = readPropertyBool("isBool");
			d.isBipolar = readPropertyBool("isBipolar");
			d.step = readPropertyIntWithDefault("step", 0);
			d.softKnobTargetSelect = readProperty("softknobTargetSelect", false).toString().toStdString();
			d.softKnobTargetList = readProperty("softknobTargetList", false).toString().toStdString();

			d.toText = valueList;

			d.page = static_cast<uint8_t>(readPropertyInt("page"));

			auto index = readPropertyInt("index");

			while(index >= 128)
			{
				index -= 128;
				++d.page;
			}
			d.index = static_cast<uint8_t>(index);

			d.range.setStart(minValue);
			d.range.setEnd(maxValue);

			d.defaultValue = defaultValue;

			if(defaultValue == Description::NoDefaultValue)
			{
				if(d.isBipolar)
					d.defaultValue = juce::roundToInt((minValue + maxValue) * 0.5f);
				else
					d.defaultValue = minValue;
			}

			d.valueList = it->second;

			{
				d.classFlags = 0;

				const auto classFlags = readPropertyString("class");

				if(!classFlags.empty())
				{
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
						if (flag == "Global")
							d.classFlags |= static_cast<int>(ParameterClass::Global);
						else if (flag == "MultiOrSingle")
							d.classFlags |= static_cast<int>(ParameterClass::MultiOrSingle);
						else if (flag == "NonPartSensitive")
							d.classFlags |= static_cast<int>(ParameterClass::NonPartSensitive);
						else
							errors << "Class " << flag << " is unknown" << std::endl;
					}
				}
			}

			m_descriptions.emplace_back(std::move(d));
		}

		for (size_t i=0; i<m_descriptions.size(); ++i)
		{
			const auto& d = m_descriptions[i];

			if(m_nameToIndex.find(d.name) != m_nameToIndex.end())
			{
				errors << "Parameter named " << d.name << " is already defined" << std::endl;
				continue;
			}
			m_nameToIndex.insert(std::make_pair(d.name, static_cast<uint32_t>(i)));
		}

		// verify soft knob parameters
		for (auto& desc : m_descriptions)
		{
			if(desc.softKnobTargetSelect.empty())
				continue;

			const auto it = m_nameToIndex.find(desc.softKnobTargetSelect);

			if(it == m_nameToIndex.end())
			{
				errors << desc.name << ": soft knob target parameter " << desc.softKnobTargetSelect << " not found" << '\n';
				continue;
			}

			if(desc.softKnobTargetList.empty())
			{
				errors << desc.name << ": soft knob target list not specified\n";
				continue;
			}

			const auto& targetParam = m_descriptions[it->second];
			auto itList = m_valueLists.find(desc.softKnobTargetList);
			if(itList == m_valueLists.end())
			{
				errors << desc.name << ": soft knob target list '" << desc.softKnobTargetList << "' not found\n";
				continue;
			}

			const auto& sourceParamNames = itList->second.texts;
			if(static_cast<int>(sourceParamNames.size()) != (targetParam.range.getLength() + 1))
			{
				errors << desc.name << ": soft knob target list " << desc.softKnobTargetList << " has " << sourceParamNames.size() << " entries but target select parameter " << targetParam.name << " has a range of " << (targetParam.range.getLength()+1) << '\n';
				continue;
			}

			for (const auto& paramName : sourceParamNames)
			{
				if(paramName.empty())
					continue;

				const auto itsourceParam = m_nameToIndex.find(paramName);

				if(itsourceParam == m_nameToIndex.end())
				{
					errors << desc.name << " - " << targetParam.name << ": soft knob source parameter " << paramName << " not found" << '\n';
					continue;
				}
			}
		}

		const auto midipackets = json["midipackets"].getDynamicObject();
		parseMidiPackets(errors, midipackets);

		const auto regions = json["regions"].getArray();
		parseParameterRegions(errors, regions);

		const auto controllers = json["controllerMap"].getArray();
		parseControllerMap(errors, controllers);

		auto res = errors.str();

		if(!res.empty())
		{
			LOG("ParameterDescription parsing issues:\n" << res);
			assert(false && "failed to parse parameter descriptions");
		}

		return res;
	}


	std::string ParameterDescriptions::parseValueList(const std::string& _key, const juce::var& _values)
	{
		auto valuesArray = _values.getArray();

		if(m_valueLists.find(_key) != m_valueLists.end())
			return "value list " + _key + " is defined twice";

		ValueList vl;

		if(valuesArray)
		{
			if(valuesArray->isEmpty())
				return std::string("value list ") + _key + " is not a valid array of strings";

			vl.texts.reserve(valuesArray->size());

			for (auto&& value : *valuesArray)
			{
				const auto text = static_cast<std::string>(value.toString().toUTF8());
				vl.texts.push_back(text);
			}

			for(uint32_t i=0; i<vl.texts.size(); ++i)
				vl.order.push_back(i);
		}
		else
		{
			const auto valueMap = _values.getDynamicObject();
			if(!valueMap)
			{
				return std::string("value list ") + _key + " neither contains an array of strings nor a map of values => strings";
			}

			std::set<uint32_t> knownValues;

			for (const auto& it : valueMap->getProperties())
			{
				const auto keyName = it.name.toString().toStdString();
				const auto& value = it.value;

				const auto key = std::strtol(keyName.c_str(), nullptr, 10);

				if(key < 0)
				{
					return std::string("Invalid parameter index '") + keyName + " in value list '" + _key + "', values must be >= 0";
				}
				if(knownValues.find(key) != knownValues.end())
				{
					return std::string("Parameter index '") + keyName + " in value list '" + _key + " has been specified twice";
				}
				knownValues.insert(key);

				const auto requiredLength = key+1;

				if(vl.texts.size() < requiredLength)
					vl.texts.resize(requiredLength);

				vl.texts[key] = value.toString().toStdString();
				vl.order.push_back(key);
			}
		}

		for(uint32_t i=0; i<vl.texts.size(); ++i)
		{
			const auto& text = vl.texts[i];

			if (vl.textToValueMap.find(text) == vl.textToValueMap.end())
				vl.textToValueMap.insert(std::make_pair(text, i));
		}

		m_valueLists.insert(std::make_pair(_key, vl));

		return {};
	}

	void ParameterDescriptions::parseMidiPackets(std::stringstream& _errors, juce::DynamicObject* _packets)
	{
		if(!_packets)
			return;

		const auto entryProps = _packets->getProperties();

		for(int i=0; i<entryProps.size(); ++i)
		{
			const auto key = std::string(entryProps.getName(i).toString().toUTF8());
			const auto& value = entryProps.getValueAt(i);

			parseMidiPacket(_errors, key, value);
		}
	}

	void ParameterDescriptions::parseMidiPacket(std::stringstream& _errors, const std::string& _key, const juce::var& _value)
	{
		if(_key.empty())
		{
			_errors << "midi packet name must not be empty" << std::endl;
			return;
		}

		if(m_midiPackets.find(_key) != m_midiPackets.end())
		{
			_errors << "midi packet with name " << _key << " is already defined" << std::endl;
			return;
		}

		const auto arr = _value.getArray();

		if(!arr)
		{
			_errors << "midi packet " << _key << " is empty" << std::endl;
			return;
		}

		std::vector<MidiPacket::MidiDataDefinition> bytes;

		for(auto i=0; i<arr->size(); ++i)
		{
			auto entry = (*arr)[i];

			auto type = entry["type"].toString().toStdString();

			MidiPacket::MidiDataDefinition byte;

			if(type == "byte")
			{
				auto value = entry["value"].toString().toStdString();

				if(value.empty())
				{
					_errors << "no value specified for type byte, midi packet " << _key << ", index " << i << std::endl;
					return;
				}

				const auto v = ::strtol(value.c_str(), nullptr, 16);

				if(v < 0 || v > 0xff)
				{
					_errors << "Midi byte must be in range 0-255" << std::endl;
					return;
				}

				byte.type = MidiDataType::Byte;
				byte.byte = static_cast<uint8_t>(v);
			}
			else if(type == "param")
			{
				byte.paramName = entry["name"].toString().toStdString();

				if(byte.paramName.empty())
				{
					_errors << "no parameter name specified for type param, midi packet " << _key << ", index " << i << std::endl;
					return;
				}

				const auto hasMask = entry.hasProperty("mask");
				const auto hasRightShift = entry.hasProperty("shift");
				const auto hasLeftShift = entry.hasProperty("shiftL");
				const auto hasPart = entry.hasProperty("part");

				if(hasMask)
				{
					const int mask = strtol(entry["mask"].toString().toStdString().c_str(), nullptr, 16);
					if(mask < 0 || mask > 0xff)
					{
						_errors << "mask needs to be between 00 and ff but got " << std::hex << mask << std::endl;
						return;
					}
					byte.paramMask = static_cast<uint8_t>(mask);
				}

				if(hasRightShift)
				{
					const int shift = entry["shift"];
					if(shift < 0 || shift > 7)
					{
						_errors << "right shift value needs to be between 0 and 7 but got " << shift << std::endl;
						return;
					}
					byte.paramShiftRight = static_cast<uint8_t>(shift);
				}

				if(hasLeftShift)
				{
					const int shift = entry["shiftL"];
					if(shift < 0 || shift > 7)
					{
						_errors << "left shift value needs to be between 0 and 7 but got " << shift << std::endl;
						return;
					}
					byte.paramShiftLeft = static_cast<uint8_t>(shift);
				}

				if(hasPart)
				{
					const int part= entry["part"];
					if(part < 0 || part > 15)
					{
						_errors << "part needs to be between 0 and 15 but got " << part << std::endl;
						return;
					}
					byte.paramPart = static_cast<uint8_t>(part);
				}

				byte.type = MidiDataType::Parameter;
			}
			else if(type == "checksum")
			{
				const int first = entry["first"];
				const int last = entry["last"];
				const int init = entry["init"];

				if(first < 0 || last < 0 || last < first)
				{
					_errors << "specified checksum range " << first << "-" << last << " is not valid, midi packet " << _key << ", index " << i << std::endl;
					return;
				}

				byte.type = MidiDataType::Checksum;
				byte.checksumFirstIndex = first;
				byte.checksumLastIndex = last;
				byte.checksumInitValue = static_cast<uint8_t>(init);
			}
			else if(type == "bank")				byte.type = MidiDataType::Bank;
			else if(type == "program")			byte.type = MidiDataType::Program;
			else if(type == "deviceid")			byte.type = MidiDataType::DeviceId;
			else if(type == "page")				byte.type = MidiDataType::Page;
			else if(type == "part")				byte.type = MidiDataType::Part;
			else if(type == "paramindex")		byte.type = MidiDataType::ParameterIndex;
			else if(type == "paramvalue")		byte.type = MidiDataType::ParameterValue;
			else if(type == "null")				byte.type = MidiDataType::Null;
			else
			{
				_errors << "Unknown midi packet data type '" << type << "', midi packet " << _key << ", index " << i << std::endl;
				return;
			}

			bytes.push_back(byte);
		}

		MidiPacket packet(_key, std::move(bytes));

		bool hasErrors = false;

		// post-read validation
		for(size_t i=0; i<packet.definitions().size(); ++i)
		{
			const auto& p = packet.definitions()[i];

			if(p.type == MidiDataType::Checksum)
			{
				if(p.checksumFirstIndex >= (packet.size()-1) || p.checksumLastIndex >= (packet.size()-1))
				{
					_errors << "specified checksum range " << p.checksumFirstIndex << "-" << p.checksumLastIndex << " is out of range 0-" << packet.size() << std::endl;
					return;
				}
			}
			else if(p.type == MidiDataType::Parameter)
			{
				uint32_t index;

				if(!getIndexByName(index, p.paramName))
				{
					hasErrors = true;
					_errors << "specified parameter " << p.paramName << " does not exist" << std::endl;
				}
			}
		}

		if(!hasErrors)
			m_midiPackets.insert(std::make_pair(_key, packet));
	}

	void ParameterDescriptions::parseParameterRegions(std::stringstream& _errors, const juce::Array<juce::var>* _regions)
	{
		if(!_regions)
			return;

		for (const auto& _region : *_regions)
			parseParameterRegion(_errors, _region);
	}

	void ParameterDescriptions::parseParameterRegion(std::stringstream& _errors, const juce::var& _value)
	{
		const auto id = _value["id"].toString().toStdString();
		const auto name = _value["name"].toString().toStdString();
		const auto parameters = _value["parameters"].getArray();
		const auto regions = _value["regions"].getArray();

		if(id.empty())
		{
			_errors << "region needs to have an id\n";
			return;
		}

		if(m_regions.find(id) != m_regions.end())
		{
			_errors << "region with id '" << id << "' already exists\n";
			return;
		}

		if(name.empty())
		{
			_errors << "region with id " << id << " needs to have a name\n";
			return;
		}

		if(!parameters && !regions)
		{
			_errors << "region with id " << id << " needs to at least one parameter or region\n";
			return;
		}

		std::unordered_map<std::string, const Description*> paramMap;

		if(parameters)
		{
			const auto& params = *parameters;

			for (const auto& i : params)
			{
				const auto& param = i.toString().toStdString();

				if(param.empty())
				{
					_errors << "Empty parameter name in parameter list for region " << id << '\n';
					return;
				}

				uint32_t idx = 0;

				if(!getIndexByName(idx, param))
				{
					_errors << "Parameter with name '" << param << "' not found for region " << id << '\n';
					return;
				}

				const auto* desc = &m_descriptions[idx];

				if(paramMap.find(param) != paramMap.end())
				{
					_errors << "Parameter with name '" << param << "' has been specified more than once for region " << id << '\n';
					return;
				}

				paramMap.insert({param, desc});
			}
		}

		if(regions)
		{
			const auto& regs = *regions;

			for (const auto& i : regs)
			{
				const auto& reg = i.toString().toStdString();

				if(reg.empty())
				{
					_errors << "Empty region specified in region '" << id << "'\n";
					return;
				}

				const auto it = m_regions.find(reg);

				if(it == m_regions.end())
				{
					_errors << "Region with id '" << reg << "' not found for region '" << id << "'\n";
					return;
				}

				const auto& region = it->second;

				const auto& regParams = region.getParams();

				for (const auto& itParam : regParams)
				{
					if(paramMap.find(itParam.first) == paramMap.end())
						paramMap.insert(itParam);
				}
			}
		}

		m_regions.insert({id, ParameterRegion(id, name, std::move(paramMap))});
	}

	void ParameterDescriptions::parseControllerMap(std::stringstream& _errors, const juce::Array<juce::var>* _controllers)
	{
		if(!_controllers)
			return;

		for (const auto& controller : *_controllers)
			parseController(_errors, controller);
	}

	void ParameterDescriptions::parseController(std::stringstream& _errors, const juce::var& _value)
	{
		const auto ccStr = _value["cc"].toString().toStdString();
		const auto ppStr = _value["pp"].toString().toStdString();
		const auto nrpnStr = _value["nrpn"].toString().toStdString();
		const auto paramName = _value["param"].toString().toStdString();

		if(ccStr.empty() && ppStr.empty())
		{
			_errors << "Controller needs to define control change (cc) or poly pressure (pp) parameter\n";
			return;
		}

		uint8_t cc = 0xff;
		uint8_t pp = 0xff;
		uint16_t nrpn = 0xffff;

		if(!ccStr.empty())
		{
			cc = static_cast<uint8_t>(::strtol(ccStr.c_str(), nullptr, 10));
			if(cc < 0 || cc > 127)
			{
				_errors << "Controller needs to be in range 0-127, param " << paramName << '\n';
				return;
			}
		}

		if(!ppStr.empty())
		{
			pp = static_cast<uint8_t>(::strtol(ppStr.c_str(), nullptr, 10));
			if(pp < 0 || pp > 127)
			{
				_errors << "Poly Pressure parameter needs to be in range 0-127, param " << paramName << '\n';
				return;
			}
		}

		if(!nrpnStr.empty())
		{
			nrpn = static_cast<uint8_t>(::strtol(nrpnStr.c_str(), nullptr, 16));
			if(nrpn < 0 || nrpn > 0x3fff)
			{
				_errors << "NRPN parameter needs to be in range $0-$3fff, param " << paramName << '\n';
				return;
			}
		}

		if(paramName.empty())
		{
			_errors << "Target parameter name 'param' must not be empty\n";
			return;
		}

		uint32_t paramIndex = 0;

		if(!getIndexByName(paramIndex, paramName))
		{
			_errors << "Parameter with name " << paramName << " not found\n";
			return;
		}

		if(cc != 0xff)
			m_controllerMap.add(synthLib::M_CONTROLCHANGE, cc, paramIndex);

		if(pp != 0xff)
			m_controllerMap.add(synthLib::M_POLYPRESSURE, pp, paramIndex);

		if(nrpn != 0xffff)
			m_controllerMap.add(ControllerMap::NrpnType, nrpn, paramIndex);
	}
}
