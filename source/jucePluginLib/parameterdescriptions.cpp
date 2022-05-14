#include "parameterdescriptions.h"

#include <cassert>

#include "../dsp56300/source/dsp56kEmu/logging.h"

namespace pluginLib
{
	ParameterDescriptions::ParameterDescriptions(const std::string& _jsonString)
	{
		const auto err = loadJson(_jsonString);
		LOG(err);
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
		for (uint32_t i=0; i<m_descriptions.size(); ++i)
		{
			const auto& description = m_descriptions[i];

			if(description.name == _name)
			{
				_index = i;
				return true;
			}
		}
		return false;
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

			d.name = name.toStdString();

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
			{
				const auto page = readPropertyString("page");
				if(page.empty())
				{
					errors << name << ": Page parameter must not be empty" << std::endl;
					break;
				}

				d.page = static_cast<uint8_t>(::strtol(page.c_str(), nullptr, 10));
			}

			m_descriptions.push_back(d);
		}
		const auto midipackets = json["midipackets"].getDynamicObject();

		parseMidiPackets(errors, midipackets);

		const auto res = errors.str();
		assert(res.empty());
		return res;
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

		std::vector<MidiPacket::MidiByte> bytes;

		for(auto i=0; i<arr->size(); ++i)
		{
			auto entry = (*arr)[i];

			auto type = entry["type"].toString().toStdString();

			MidiPacket::MidiByte byte;

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
				byte.name = entry["name"].toString().toStdString();

				if(byte.name.empty())
				{
					_errors << "no parameter name specified for type param, midi packet " << _key << ", index " << i << std::endl;
					return;
				}

				byte.type = MidiDataType::Parameter;
			}
			else if(type == "checksum")
			{
				const int first = entry["first"];
				const int last = entry["last"];
				const int init = entry["init"];

				if(first < 0 || last < 0 || last <= first)
				{
					_errors << "specified checksum range " << first << "-" << last << " is not valid, midi packet " << _key << ", index " << i << std::endl;
					return;
				}

				byte.type = MidiDataType::Checksum;
				byte.checksumFirstIndex = first;
				byte.checksumLastIndex = last;
				byte.checksumInitValue = init;
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
				_errors << "Unknown midi packet data type " << type << ", midi packet " << _key << ", index " << i << std::endl;
				return;
			}

			bytes.push_back(byte);
		}

		MidiPacket packet;
		packet.bytes = bytes;

		// post-read validation
		for(size_t i=0; i<packet.bytes.size(); ++i)
		{
			const auto& p = packet.bytes[i];

			if(p.type == MidiDataType::Checksum)
			{
				if(p.checksumFirstIndex >= (packet.bytes.size()-1) || p.checksumLastIndex >= (packet.bytes.size()-1))
				{
					_errors << "specified checksum range " << p.checksumFirstIndex << "-" << p.checksumLastIndex << " is out of range 0-" << packet.bytes.size() << i << std::endl;
					return;
				}
			}
			else if(p.type == MidiDataType::Parameter)
			{
				uint32_t index;

				if(!getIndexByName(index, p.name))
				{
					_errors << "specified parameter " << p.name << " does not exist" << std::endl;
					return;
				}
			}
		}

		m_midiPackets.insert(std::make_pair(_key, packet));
	}
}
