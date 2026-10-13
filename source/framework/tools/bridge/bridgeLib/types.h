#pragma once

#include <cstdint>

#include "dsp56kBase/buildconfig.h"

namespace bridgeLib
{
	static constexpr uint32_t g_udpServerPort   = 56303;
	static constexpr uint32_t g_tcpServerPort   = 56362;

	// Clients and the server only talk to each other if this matches. The server loads every plugin library built with
	// the same value, whatever its plugin version, and serves each client from the library with exactly the client's
	// plugin name, 4CC and version. A bump costs users: DSPBridgeServer and the plugins it hosts must be updated together.
	//
	// Bump it in the commit that changes
	// 1. a network command or its serialization (commands.*, command*.*, tcpConnection.cpp for MIDI and audio),
	// 2. an export in client/export.h or client/plugin.h,
	// 3. the virtual functions of synthLib::Device: adding, removing, reordering or changing the signature of any of
	//    them, even one the server never calls, their vtable slots are positional,
	// 4. the layout of anything the server passes to a plugin library or reads back from it: synthLib::DeviceCreateParams
	//    (with baseLib::MD5), SMidiEvent and the SysexBuffer type (midiTypes.h), the audio types, StateType, or the
	//    std::vector / std::string ABI of the toolchain.
	// No bump for the content passing through (a device's MIDI, audio or state format), for data members or non-virtual
	// functions of synthLib::Device (the server only makes virtual calls on a device, a library creates and destroys its
	// devices itself; check server/ when that changes), or for making a pure virtual function non-pure.
	// At most one bump per release: the first change after a release bumps, later ones before the next release ride on it.
	//
	// 1'00'05 since 2.2.26: synthLib::Device::getStateWithPendingMidi; hasArpeggiators, processArpeggiators and
	//          onHostTransport of synthLib::Device, the host's transport in SMidiEvent and in Command::Midi
	static constexpr uint32_t g_protocolVersion = 1'00'05;

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
