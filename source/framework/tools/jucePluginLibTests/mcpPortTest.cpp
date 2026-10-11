#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "jucePluginLibTests.h"

#include <iostream>
#include <memory>
#include <string>

#include "mcpServerLib/mcpServer.h"

#include "juce_core/juce_core.h"

namespace
{
	// above the ports of the MCP servers running on this machine, 13710 and up
	constexpr int g_basePort = 14710;

	// what an older build leaves on a port: a socket on 0.0.0.0. Bound only, so it raises no firewall prompt
	struct WildcardSocket
	{
		explicit WildcardSocket(const int _port) : handle(::socket(AF_INET, SOCK_STREAM, 0))
		{
			sockaddr_in sa{};
			sa.sin_family = AF_INET;
			sa.sin_port = htons(static_cast<uint16_t>(_port));
			bound = ::bind(handle, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == 0;
		}

		~WildcardSocket()
		{
#ifdef _WIN32
			closesocket(handle);
#else
			close(handle);
#endif
		}

		decltype(::socket(0, 0, 0)) handle;
		bool bound = false;
	};

	bool answersOnLoopback(const int _port)
	{
		juce::StreamingSocket s;
		if (!s.connect("127.0.0.1", _port, 2000))
			return false;

		const std::string request = "GET /health HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
		s.write(request.data(), static_cast<int>(request.size()));

		std::string response;
		char buf[256];
		while (response.find("\"status\"") == std::string::npos && s.waitUntilReady(true, 2000) == 1)
		{
			const auto count = s.read(buf, sizeof(buf), false);
			if (count <= 0)
				break;
			response.append(buf, static_cast<size_t>(count));
		}
		return response.find("\"status\"") != std::string::npos;
	}
}

void testMcpPorts()
{
	std::cout << "Testing that MCP servers listen on loopback and skip the ports others hold..." << std::endl;

	mcpServer::McpServer a(g_basePort);
	TEST_ASSERT(a.start());
	TEST_ASSERT(answersOnLoopback(a.getPort()));

	// and nowhere else
	for (const auto& ip : juce::IPAddress::getAllAddresses())
	{
		if (ip.address[0] == 127 || ip.isNull())
			continue;
		juce::StreamingSocket s;
		TEST_ASSERT(!s.connect(ip.toString(), a.getPort(), 500));
		break;
	}

	// an older build, listening on all interfaces, must not be able to join a port a loopback server holds
	TEST_ASSERT(!WildcardSocket(a.getPort()).bound);

	mcpServer::McpServer b(a.getPort());
	TEST_ASSERT(b.start());
	TEST_ASSERT(b.getPort() > a.getPort());

	// and a loopback server must not join a port an older build holds
	int held = b.getPort() + 1;
	auto old = std::make_unique<WildcardSocket>(held);
	while (!old->bound && held < b.getPort() + 100)
		old = std::make_unique<WildcardSocket>(++held);
	TEST_ASSERT(old->bound);

	mcpServer::McpServer c(held);
	TEST_ASSERT(c.start());
	TEST_ASSERT(c.getPort() > held);
	TEST_ASSERT(answersOnLoopback(c.getPort()));

	std::cout << "  ports " << a.getPort() << ", " << b.getPort() << ", " << c.getPort() << ", " << held << " held on 0.0.0.0" << std::endl;
}
