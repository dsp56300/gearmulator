#pragma once

#include "jsonHelpers.h"
#include "mcpTypes.h"

#include <optional>
#include <string>

namespace mcpServer
{
	// JSON-RPC 2.0 request
	struct JsonRpcRequest
	{
		std::string jsonrpc = "2.0";
		std::string method;
		JsonValue params;
		JsonValue id;	// string, int, or null

		bool isNotification() const { return id.isVoid(); }
	};

	// JSON-RPC 2.0 response
	struct JsonRpcResponse
	{
		JsonValue id;
		JsonValue result;
		bool isError = false;
		ErrorCode errorCode = ErrorCode::InternalError;
		std::string errorMessage;

		static JsonRpcResponse success(const JsonValue& _id, const JsonValue& _result)
		{
			JsonRpcResponse r;
			r.id = _id;
			r.result = _result;
			return r;
		}

		static JsonRpcResponse error(const JsonValue& _id, ErrorCode _code, const std::string& _message)
		{
			JsonRpcResponse r;
			r.id = _id;
			r.isError = true;
			r.errorCode = _code;
			r.errorMessage = _message;
			return r;
		}
	};

	// Parse a JSON-RPC request from a JSON string
	inline std::optional<JsonRpcRequest> parseJsonRpcRequest(const std::string& _json)
	{
		auto val = JsonValue::parse(_json);
		if (!val.isObject())
			return std::nullopt;

		JsonRpcRequest req;
		req.jsonrpc = val.get("jsonrpc").getString().toStdString();
		req.method = val.get("method").getString().toStdString();
		req.params = val.get("params");
		req.id = val.get("id");

		if (req.jsonrpc != "2.0" || req.method.empty())
			return std::nullopt;

		return req;
	}

	// Serialize a JSON-RPC response to a JSON string
	inline std::string serializeJsonRpcResponse(const JsonRpcResponse& _response)
	{
		auto obj = JsonValue::object();
		obj.set("jsonrpc", JsonValue::fromString("2.0"));
		obj.set("id", _response.id);

		if (_response.isError)
		{
			auto err = JsonValue::object();
			err.set("code", JsonValue::fromInt(static_cast<int>(_response.errorCode)));
			err.set("message", JsonValue::fromString(_response.errorMessage));
			obj.set("error", err);
		}
		else
		{
			obj.set("result", _response.result);
		}

		return obj.toJsonString();
	}
}
