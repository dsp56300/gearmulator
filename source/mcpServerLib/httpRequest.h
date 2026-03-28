#pragma once

#include <map>
#include <string>

namespace mcpServer
{
	struct HttpRequest
	{
		std::string method;
		std::string path;
		std::string httpVersion;
		std::map<std::string, std::string> headers;
		std::string body;

		bool isPost() const { return method == "POST"; }
		bool isGet() const { return method == "GET"; }
		bool isOptions() const { return method == "OPTIONS"; }

		std::string getHeader(const std::string& _name) const
		{
			const auto it = headers.find(_name);
			return it != headers.end() ? it->second : std::string();
		}

		int getContentLength() const
		{
			const auto val = getHeader("content-length");
			return val.empty() ? 0 : std::stoi(val);
		}
	};
}
