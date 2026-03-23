#include "mcpPluginServer.h"

#include "discoveryFile.h"

#include "jucePluginLib/controller.h"
#include "jucePluginLib/parameter.h"
#include "jucePluginLib/processor.h"

#include "synthLib/plugin.h"
#include "synthLib/device.h"
#include "synthLib/deviceTypes.h"
#include "synthLib/midiTypes.h"

#include "networkLib/logging.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <sstream>
#include <thread>
#include <chrono>

namespace mcpServer
{
	McpPluginServer::McpPluginServer(pluginLib::Processor& _processor, const int _port)
		: m_processor(_processor)
		, m_server(_port)
	{
		const auto& props = m_processor.getProperties();
		m_server.setServerName("Gearmulator MCP - " + props.name);

		registerTools();
	}

	McpPluginServer::~McpPluginServer()
	{
		stop();
	}

	bool McpPluginServer::start()
	{
		if (!m_server.start())
			return false;

		// Register in discovery file
		const auto& props = m_processor.getProperties();
		DiscoveryEntry entry;
		entry.pluginName = props.name;
		entry.plugin4CC = props.plugin4CC;
		entry.port = m_server.getPort();
		entry.pid = static_cast<int>(
#ifdef _WIN32
			GetCurrentProcessId()
#else
			getpid()
#endif
		);
		DiscoveryFile::registerInstance(entry);

		return true;
	}

	void McpPluginServer::stop()
	{
		if (m_server.isRunning())
		{
			DiscoveryFile::unregisterInstance(m_server.getPort());
		}
		m_server.stop();
	}

	bool McpPluginServer::isRunning() const
	{
		return m_server.isRunning();
	}

	int McpPluginServer::getPort() const
	{
		return m_server.getPort();
	}

	void McpPluginServer::registerTools()
	{
		registerParameterTools();
		registerMidiTools();
		registerStateTools();
		registerDeviceInfoTools();
	}

	void McpPluginServer::registerParameterTools()
	{
		// list_parameters
		{
			ToolDef tool;
			tool.name = "list_parameters";
			tool.description = "List all parameters with their current values, ranges, and metadata for a given part";
			tool.inputSchema.addIntProperty("part", "Part number (0-15)", false, 0, 15);
			tool.handler = [this](const JsonValue& _params) -> JsonValue
			{
				if (!m_processor.hasController())
					throw std::runtime_error("Controller not available");

				auto& controller = m_processor.getController();
				const uint8_t part = _params.isObject() && _params.hasProperty("part")
					? static_cast<uint8_t>(_params.get("part").getInt())
					: controller.getCurrentPart();

				auto result = JsonValue::array();

				const auto& params = controller.getExposedParameters();
				for (const auto& [idx, paramList] : params)
				{
					if (paramList.empty())
						continue;

					for (const auto* param : paramList)
					{
						if (param->getPart() != part)
							continue;

						auto p = JsonValue::object();
						p.set("name", JsonValue::fromString(param->getDescription().name));
						p.set("displayName", JsonValue::fromString(
							param->getDescription().displayName.empty()
								? param->getDescription().name
								: param->getDescription().displayName));
						p.set("value", JsonValue::fromInt(param->getUnnormalizedValue()));
						p.set("min", JsonValue::fromInt(param->getDescription().range.getStart()));
						p.set("max", JsonValue::fromInt(param->getDescription().range.getEnd()));
						p.set("text", JsonValue::fromString(param->getText(param->getValue(), 64).toStdString()));
						p.set("part", JsonValue::fromInt(part));
						p.set("page", JsonValue::fromInt(param->getDescription().page));
						p.set("index", JsonValue::fromInt(param->getDescription().index));
						p.set("isDiscrete", JsonValue::fromBool(param->isDiscrete()));
						p.set("isBool", JsonValue::fromBool(param->isBoolean()));
						p.set("isBipolar", JsonValue::fromBool(param->isBipolar()));
						result.append(p);
						break; // Only first param in the list per index
					}
				}

				return result;
			};
			m_server.registerTool(std::move(tool));
		}

		// get_parameter
		{
			ToolDef tool;
			tool.name = "get_parameter";
			tool.description = "Get a specific parameter's value and metadata by name";
			tool.inputSchema.addProperty("name", "string", "Parameter name", true);
			tool.inputSchema.addIntProperty("part", "Part number (0-15)", false, 0, 15);
			tool.handler = [this](const JsonValue& _params) -> JsonValue
			{
				if (!m_processor.hasController())
					throw std::runtime_error("Controller not available");

				auto& controller = m_processor.getController();
				const auto name = _params.get("name").getString().toStdString();
				const uint8_t part = _params.hasProperty("part")
					? static_cast<uint8_t>(_params.get("part").getInt())
					: controller.getCurrentPart();

				auto* param = controller.getParameter(name, part);
				if (!param)
					throw std::runtime_error("Parameter not found: " + name);

				auto result = JsonValue::object();
				result.set("name", JsonValue::fromString(param->getDescription().name));
				result.set("displayName", JsonValue::fromString(
					param->getDescription().displayName.empty()
						? param->getDescription().name
						: param->getDescription().displayName));
				result.set("value", JsonValue::fromInt(param->getUnnormalizedValue()));
				result.set("min", JsonValue::fromInt(param->getDescription().range.getStart()));
				result.set("max", JsonValue::fromInt(param->getDescription().range.getEnd()));
				result.set("text", JsonValue::fromString(param->getText(param->getValue(), 64).toStdString()));
				result.set("part", JsonValue::fromInt(part));
				result.set("isLocked", JsonValue::fromBool(param->isLocked()));

				// Include value list if available
				const auto& valueList = param->getDescription().valueList;
				if (!valueList.texts.empty())
				{
					auto values = JsonValue::array();
					for (const auto& text : valueList.texts)
					{
						if (!text.empty())
							values.append(JsonValue::fromString(text));
					}
					result.set("valueList", values);
				}

				return result;
			};
			m_server.registerTool(std::move(tool));
		}

		// set_parameter
		{
			ToolDef tool;
			tool.name = "set_parameter";
			tool.description = "Set a parameter value by name";
			tool.inputSchema.addProperty("name", "string", "Parameter name", true);
			tool.inputSchema.addIntProperty("value", "New parameter value", true);
			tool.inputSchema.addIntProperty("part", "Part number (0-15)", false, 0, 15);
			tool.handler = [this](const JsonValue& _params) -> JsonValue
			{
				if (!m_processor.hasController())
					throw std::runtime_error("Controller not available");

				auto& controller = m_processor.getController();
				const auto name = _params.get("name").getString().toStdString();
				const int value = _params.get("value").getInt();
				const uint8_t part = _params.hasProperty("part")
					? static_cast<uint8_t>(_params.get("part").getInt())
					: controller.getCurrentPart();

				auto* param = controller.getParameter(name, part);
				if (!param)
					throw std::runtime_error("Parameter not found: " + name);

				param->setUnnormalizedValueNotifyingHost(value, pluginLib::Parameter::Origin::Ui);

				auto result = JsonValue::object();
				result.set("success", JsonValue::fromBool(true));
				result.set("name", JsonValue::fromString(name));
				result.set("value", JsonValue::fromInt(param->getUnnormalizedValue()));
				result.set("text", JsonValue::fromString(param->getText(param->getValue(), 64).toStdString()));
				return result;
			};
			m_server.registerTool(std::move(tool));
		}

		// set_parameters_batch
		{
			ToolDef tool;
			tool.name = "set_parameters_batch";
			tool.description = "Set multiple parameters at once. Pass a JSON array of {name, value} objects";
			tool.inputSchema.addIntProperty("part", "Part number (0-15)", false, 0, 15);
			tool.handler = [this](const JsonValue& _params) -> JsonValue
			{
				if (!m_processor.hasController())
					throw std::runtime_error("Controller not available");

				auto& controller = m_processor.getController();
				const uint8_t part = _params.hasProperty("part")
					? static_cast<uint8_t>(_params.get("part").getInt())
					: controller.getCurrentPart();

				const auto parameters = _params.get("parameters");
				if (!parameters.isArray())
					throw std::runtime_error("'parameters' must be an array of {name, value} objects");

				auto results = JsonValue::array();
				const int count = parameters.getArraySize();
				for (int i = 0; i < count; ++i)
				{
					const auto entry = parameters.getArrayElement(i);
					const auto name = entry.get("name").getString().toStdString();
					const int value = entry.get("value").getInt();

					auto* param = controller.getParameter(name, part);

					auto r = JsonValue::object();
					r.set("name", JsonValue::fromString(name));

					if (param)
					{
						param->setUnnormalizedValueNotifyingHost(value, pluginLib::Parameter::Origin::Ui);
						r.set("success", JsonValue::fromBool(true));
						r.set("value", JsonValue::fromInt(param->getUnnormalizedValue()));
					}
					else
					{
						r.set("success", JsonValue::fromBool(false));
						r.set("error", JsonValue::fromString("Parameter not found"));
					}
					results.append(r);
				}

				return results;
			};
			m_server.registerTool(std::move(tool));
		}
	}

	synthLib::MidiEventSource McpPluginServer::parseMidiSource(const JsonValue& _params)
	{
		if (_params.hasProperty("source"))
		{
			const auto src = _params.get("source").getString();
			if (src == "host")
				return synthLib::MidiEventSource::Host;
			if (src == "physical")
				return synthLib::MidiEventSource::Physical;
		}
		return synthLib::MidiEventSource::Editor;
	}

	void McpPluginServer::registerMidiTools()
	{
		// send_midi
		{
			ToolDef tool;
			tool.name = "send_midi";
			tool.description = "Send a raw MIDI message (status byte, data1, data2)";
			tool.inputSchema.addIntProperty("status", "MIDI status byte (e.g. 0x90 for note on)", true, 0, 255);
			tool.inputSchema.addIntProperty("data1", "First data byte", true, 0, 127);
			tool.inputSchema.addIntProperty("data2", "Second data byte", false, 0, 127);
			tool.inputSchema.addProperty("source", "string", "MIDI source: 'editor' (default), 'host', 'physical'", false);
			tool.handler = [this](const JsonValue& _params) -> JsonValue
			{
				synthLib::SMidiEvent ev(parseMidiSource(_params));
				ev.a = static_cast<uint8_t>(_params.get("status").getInt());
				ev.b = static_cast<uint8_t>(_params.get("data1").getInt());
				ev.c = _params.hasProperty("data2") ? static_cast<uint8_t>(_params.get("data2").getInt()) : static_cast<uint8_t>(0);

				m_processor.addMidiEvent(ev);

				auto result = JsonValue::object();
				result.set("success", JsonValue::fromBool(true));
				return result;
			};
			m_server.registerTool(std::move(tool));
		}

		// send_note
		{
			ToolDef tool;
			tool.name = "send_note";
			tool.description = "Send a note on, wait for a duration, then send note off";
			tool.inputSchema.addIntProperty("note", "MIDI note number (0-127, e.g. 60=C4)", true, 0, 127);
			tool.inputSchema.addIntProperty("velocity", "Note velocity (1-127)", false, 1, 127);
			tool.inputSchema.addIntProperty("channel", "MIDI channel (0-15)", false, 0, 15);
			tool.inputSchema.addIntProperty("duration_ms", "Note duration in milliseconds", false, 1, 10000);
			tool.inputSchema.addProperty("source", "string", "MIDI source: 'editor' (default), 'host', 'physical'", false);
			tool.handler = [this](const JsonValue& _params) -> JsonValue
			{
				const uint8_t note = static_cast<uint8_t>(_params.get("note").getInt());
				const uint8_t velocity = _params.hasProperty("velocity")
					? static_cast<uint8_t>(_params.get("velocity").getInt()) : static_cast<uint8_t>(100);
				const uint8_t channel = _params.hasProperty("channel")
					? static_cast<uint8_t>(_params.get("channel").getInt()) : static_cast<uint8_t>(0);
				const int durationMs = _params.hasProperty("duration_ms")
					? _params.get("duration_ms").getInt() : 500;
				const auto source = parseMidiSource(_params);

				// Note on
				synthLib::SMidiEvent noteOn(source);
				noteOn.a = static_cast<uint8_t>(0x90 | (channel & 0x0f));
				noteOn.b = note;
				noteOn.c = velocity;
				m_processor.addMidiEvent(noteOn);

				// Wait
				std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));

				// Note off
				synthLib::SMidiEvent noteOff(source);
				noteOff.a = static_cast<uint8_t>(0x80 | (channel & 0x0f));
				noteOff.b = note;
				noteOff.c = 0;
				m_processor.addMidiEvent(noteOff);

				auto result = JsonValue::object();
				result.set("success", JsonValue::fromBool(true));
				result.set("note", JsonValue::fromInt(note));
				result.set("duration_ms", JsonValue::fromInt(durationMs));
				return result;
			};
			m_server.registerTool(std::move(tool));
		}

		// send_sysex
		{
			ToolDef tool;
			tool.name = "send_sysex";
			tool.description = "Send a SysEx message. Provide hex bytes as a string (e.g. 'F0 00 20 33 ... F7')";
			tool.inputSchema.addProperty("hex", "string", "Hex string of sysex bytes separated by spaces", true);
			tool.handler = [this](const JsonValue& _params) -> JsonValue
			{
				const auto hexStr = _params.get("hex").getString().toStdString();

				synthLib::SMidiEvent ev(synthLib::MidiEventSource::Editor);
				std::istringstream iss(hexStr);
				std::string byteStr;
				while (iss >> byteStr)
					ev.sysex.push_back(static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16)));

				if (ev.sysex.size() < 2 || ev.sysex.front() != 0xF0 || ev.sysex.back() != 0xF7)
					throw std::runtime_error("SysEx must start with F0 and end with F7");

				m_processor.addMidiEvent(ev);

				auto result = JsonValue::object();
				result.set("success", JsonValue::fromBool(true));
				result.set("byteCount", JsonValue::fromInt(static_cast<int>(ev.sysex.size())));
				return result;
			};
			m_server.registerTool(std::move(tool));
		}

		// send_program_change
		{
			ToolDef tool;
			tool.name = "send_program_change";
			tool.description = "Send a MIDI program change message";
			tool.inputSchema.addIntProperty("program", "Program number (0-127)", true, 0, 127);
			tool.inputSchema.addIntProperty("channel", "MIDI channel (0-15)", false, 0, 15);
			tool.handler = [this](const JsonValue& _params) -> JsonValue
			{
				const uint8_t program = static_cast<uint8_t>(_params.get("program").getInt());
				const uint8_t channel = _params.hasProperty("channel")
					? static_cast<uint8_t>(_params.get("channel").getInt()) : static_cast<uint8_t>(0);

				synthLib::SMidiEvent ev(synthLib::MidiEventSource::Editor);
				ev.a = static_cast<uint8_t>(0xC0 | (channel & 0x0f));
				ev.b = program;
				m_processor.addMidiEvent(ev);

				auto result = JsonValue::object();
				result.set("success", JsonValue::fromBool(true));
				result.set("program", JsonValue::fromInt(program));
				result.set("channel", JsonValue::fromInt(channel));
				return result;
			};
			m_server.registerTool(std::move(tool));
		}
	}

	void McpPluginServer::registerStateTools()
	{
		// get_state
		{
			ToolDef tool;
			tool.name = "get_state";
			tool.description = "Get the current device state (global or current program) as a base64-encoded binary";
			tool.inputSchema.addEnumProperty("type", "State type to retrieve", {"global", "currentProgram"}, true);
			tool.handler = [this](const JsonValue& _params) -> JsonValue
			{
				const auto typeStr = _params.get("type").getString().toStdString();
				const auto stateType = (typeStr == "global")
					? synthLib::StateTypeGlobal
					: synthLib::StateTypeCurrentProgram;

				std::vector<uint8_t> state;
				if (!m_processor.getPlugin().getState(state, stateType))
					throw std::runtime_error("Failed to get device state");

				const auto base64 = juce::Base64::toBase64(state.data(), state.size()).toStdString();

				auto result = JsonValue::object();
				result.set("type", JsonValue::fromString(typeStr));
				result.set("size", JsonValue::fromInt(static_cast<int>(state.size())));
				result.set("data", JsonValue::fromString(base64));
				return result;
			};
			m_server.registerTool(std::move(tool));
		}

		// set_state
		{
			ToolDef tool;
			tool.name = "set_state";
			tool.description = "Load a device state from base64-encoded binary data";
			tool.inputSchema.addProperty("data", "string", "Base64-encoded state data", true);
			tool.handler = [this](const JsonValue& _params) -> JsonValue
			{
				const auto base64Str = _params.get("data").getString();

				juce::MemoryOutputStream decoded;
				if (!juce::Base64::convertFromBase64(decoded, base64Str))
					throw std::runtime_error("Invalid base64 data");

				const auto* data = static_cast<const uint8_t*>(decoded.getData());
				const std::vector<uint8_t> state(data, data + decoded.getDataSize());

				if (!m_processor.getPlugin().setState(state))
					throw std::runtime_error("Failed to set device state");

				auto result = JsonValue::object();
				result.set("success", JsonValue::fromBool(true));
				result.set("size", JsonValue::fromInt(static_cast<int>(state.size())));
				return result;
			};
			m_server.registerTool(std::move(tool));
		}

		// get_current_part
		{
			ToolDef tool;
			tool.name = "get_current_part";
			tool.description = "Get the currently selected part number";
			tool.handler = [this](const JsonValue&) -> JsonValue
			{
				if (!m_processor.hasController())
					throw std::runtime_error("Controller not available");

				auto result = JsonValue::object();
				result.set("part", JsonValue::fromInt(m_processor.getController().getCurrentPart()));
				result.set("partCount", JsonValue::fromInt(m_processor.getController().getPartCount()));
				return result;
			};
			m_server.registerTool(std::move(tool));
		}

		// set_current_part
		{
			ToolDef tool;
			tool.name = "set_current_part";
			tool.description = "Switch the active part";
			tool.inputSchema.addIntProperty("part", "Part number", true, 0, 15);
			tool.handler = [this](const JsonValue& _params) -> JsonValue
			{
				if (!m_processor.hasController())
					throw std::runtime_error("Controller not available");

				const uint8_t part = static_cast<uint8_t>(_params.get("part").getInt());
				m_processor.getController().setCurrentPart(part);

				auto result = JsonValue::object();
				result.set("success", JsonValue::fromBool(true));
				result.set("part", JsonValue::fromInt(part));
				return result;
			};
			m_server.registerTool(std::move(tool));
		}
	}

	void McpPluginServer::registerDeviceInfoTools()
	{
		// get_device_info
		{
			ToolDef tool;
			tool.name = "get_device_info";
			tool.description = "Get device information: model, sample rate, channel count, DSP clock, validity";
			tool.handler = [this](const JsonValue&) -> JsonValue
			{
				auto& plugin = m_processor.getPlugin();

				auto result = JsonValue::object();
				result.set("valid", JsonValue::fromBool(plugin.isValid()));
				result.set("hostSamplerate", JsonValue::fromDouble(static_cast<double>(plugin.getHostSamplerate())));
				result.set("dspClockPercent", JsonValue::fromInt(static_cast<int>(m_processor.getDspClockPercent())));
				result.set("dspClockHz", JsonValue::fromInt64(static_cast<int64_t>(m_processor.getDspClockHz())));
				result.set("canModifyDspClock", JsonValue::fromBool(m_processor.canModifyDspClock()));
				result.set("outputGain", JsonValue::fromDouble(static_cast<double>(m_processor.getOutputGain())));

				if (m_processor.hasController())
				{
					auto& controller = m_processor.getController();
					result.set("currentPart", JsonValue::fromInt(controller.getCurrentPart()));
					result.set("partCount", JsonValue::fromInt(controller.getPartCount()));
				}

				return result;
			};
			m_server.registerTool(std::move(tool));
		}

		// get_plugin_info
		{
			ToolDef tool;
			tool.name = "get_plugin_info";
			tool.description = "Get plugin information: name, version, format identifier";
			tool.handler = [this](const JsonValue&) -> JsonValue
			{
				const auto& props = m_processor.getProperties();

				auto result = JsonValue::object();
				result.set("name", JsonValue::fromString(props.name));
				result.set("vendor", JsonValue::fromString(props.vendor));
				result.set("plugin4CC", JsonValue::fromString(props.plugin4CC));
				result.set("isSynth", JsonValue::fromBool(props.isSynth));
				result.set("wantsMidiInput", JsonValue::fromBool(props.wantsMidiInput));
				result.set("producesMidiOut", JsonValue::fromBool(props.producesMidiOut));
				result.set("mcpPort", JsonValue::fromInt(m_server.getPort()));
				return result;
			};
			m_server.registerTool(std::move(tool));
		}

		// dump_all_parameters
		{
			ToolDef tool;
			tool.name = "dump_all_parameters";
			tool.description = "Dump all parameter values for all parts as a snapshot for testing/comparison";
			tool.handler = [this](const JsonValue&) -> JsonValue
			{
				if (!m_processor.hasController())
					throw std::runtime_error("Controller not available");

				auto& controller = m_processor.getController();
				auto result = JsonValue::object();

				const uint8_t partCount = controller.getPartCount();
				auto parts = JsonValue::array();

				for (uint8_t part = 0; part < partCount; ++part)
				{
					auto partObj = JsonValue::object();
					partObj.set("part", JsonValue::fromInt(part));

					auto params = JsonValue::object();
					const auto& paramMap = controller.getExposedParameters();

					for (const auto& [idx, paramList] : paramMap)
					{
						for (const auto* param : paramList)
						{
							if (param->getPart() != part)
								continue;
							params.set(param->getDescription().name,
								JsonValue::fromInt(param->getUnnormalizedValue()));
							break;
						}
					}

					partObj.set("parameters", params);
					parts.append(partObj);
				}

				result.set("parts", parts);
				result.set("partCount", JsonValue::fromInt(partCount));
				return result;
			};
			m_server.registerTool(std::move(tool));
		}
	}
}
