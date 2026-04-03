#pragma once

#include "jsonHelpers.h"

#include <functional>
#include <limits>
#include <string>

namespace mcpServer
{
	// Schema for a tool's input parameters (JSON Schema object)
	struct ToolInputSchema
	{
		std::string type = "object";
		JsonValue properties = JsonValue::object();
		JsonValue required = JsonValue::array();

		void addProperty(const std::string& _name, const std::string& _type, const std::string& _description, bool _required = false)
		{
			auto prop = JsonValue::object();
			prop.set("type", JsonValue::fromString(_type));
			prop.set("description", JsonValue::fromString(_description));
			properties.set(_name, prop);
			if (_required)
				required.append(JsonValue::fromString(_name));
		}

		void addEnumProperty(const std::string& _name, const std::string& _description, const std::vector<std::string>& _values, bool _required = false)
		{
			auto prop = JsonValue::object();
			prop.set("type", JsonValue::fromString("string"));
			prop.set("description", JsonValue::fromString(_description));
			auto enumArr = JsonValue::array();
			for (const auto& v : _values)
				enumArr.append(JsonValue::fromString(v));
			prop.set("enum", enumArr);
			properties.set(_name, prop);
			if (_required)
				required.append(JsonValue::fromString(_name));
		}

		void addIntProperty(const std::string& _name, const std::string& _description, bool _required = false, int _min = std::numeric_limits<int>::min(), int _max = std::numeric_limits<int>::max())
		{
			auto prop = JsonValue::object();
			prop.set("type", JsonValue::fromString("integer"));
			prop.set("description", JsonValue::fromString(_description));
			if (_min != std::numeric_limits<int>::min())
				prop.set("minimum", JsonValue::fromInt(_min));
			if (_max != std::numeric_limits<int>::max())
				prop.set("maximum", JsonValue::fromInt(_max));
			properties.set(_name, prop);
			if (_required)
				required.append(JsonValue::fromString(_name));
		}

		JsonValue toJson() const
		{
			auto schema = JsonValue::object();
			schema.set("type", JsonValue::fromString(type));
			schema.set("properties", properties);
			schema.set("required", required);
			return schema;
		}
	};

	// Tool handler function: takes params, returns result
	using ToolHandler = std::function<JsonValue(const JsonValue& _params)>;

	// Tool definition
	struct ToolDef
	{
		std::string name;
		std::string description;
		ToolInputSchema inputSchema;
		ToolHandler handler;

		JsonValue toJson() const
		{
			auto tool = JsonValue::object();
			tool.set("name", JsonValue::fromString(name));
			tool.set("description", JsonValue::fromString(description));
			tool.set("inputSchema", inputSchema.toJson());
			return tool;
		}
	};
}
