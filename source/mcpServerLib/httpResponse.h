#pragma once

#include <map>
#include <string>
#include <sstream>

namespace mcpServer
{
	struct HttpResponse
	{
		int statusCode = 200;
		std::string statusText = "OK";
		std::map<std::string, std::string> headers;
		std::string body;

		void setJsonBody(const std::string& _json)
		{
			body = _json;
			headers["Content-Type"] = "application/json";
			headers["Content-Length"] = std::to_string(body.size());
		}

		void setSseHeaders()
		{
			headers["Content-Type"] = "text/event-stream";
			headers["Cache-Control"] = "no-cache";
			headers["Connection"] = "keep-alive";
		}

		void setCorsHeaders()
		{
			headers["Access-Control-Allow-Origin"] = "*";
			headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS";
			headers["Access-Control-Allow-Headers"] = "Content-Type, Accept";
		}

		std::string serialize() const
		{
			std::ostringstream ss;
			ss << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n";
			for (const auto& [key, value] : headers)
				ss << key << ": " << value << "\r\n";
			ss << "\r\n";
			if (!body.empty())
				ss << body;
			return ss.str();
		}
	};
}
