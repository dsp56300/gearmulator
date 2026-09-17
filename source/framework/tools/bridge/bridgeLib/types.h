#pragma once

#include <cstdint>

#include "dsp56kBase/buildconfig.h"

namespace bridgeLib
{
	static constexpr uint32_t g_udpServerPort   = 56303;
	static constexpr uint32_t g_tcpServerPort   = 56362;

	// Clients and the server only talk to each other if this matches, and the server only loads plugins built with the
	// same value, whatever their plugin version. Bump it whenever the network protocol changes, and whenever anything
	// the server passes to a plugin library changes its binary layout: synthLib::Device and its virtual functions,
	// synthLib::DeviceCreateParams, SMidiEvent, the audio types, bridgeLib::PluginDesc and the bridge* exports.
	static constexpr uint32_t g_protocolVersion = 1'00'03;

	using SessionId = uint64_t;

	enum class Platform
	{
		Windows,
		MacOS,
		Unix
	};

	enum class Arch
	{
		X86,
		X64,
		Aarch64
	};

#ifdef _WIN32
	static constexpr Platform g_platform = Platform::Windows;
#elif defined(__APPLE__)
	static constexpr Platform g_platform = Platform::MacOS;
#elif defined(__linux__) || defined(__unix__)
	static constexpr Platform g_platform = Platform::Unix;
#endif

#ifdef HAVE_ARM64
	static constexpr Arch g_arch = Arch::Aarch64;
#elif defined(HAVE_X86_64)
	static constexpr Arch g_arch = Arch::X64;
#elif defined(_M_IX86) || defined(__i386__)
	static constexpr Arch g_arch = Arch::X86;
#else
	static_assert(false, "Unknown architecture");
#endif
}
