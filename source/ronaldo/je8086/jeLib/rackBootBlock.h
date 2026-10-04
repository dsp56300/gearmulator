#pragma once

#include <cstdint>
#include <vector>

namespace jeLib
{
	// The JP-8080 update files carry the 896 KB application only. The 64 KB flash block in front of it holds the
	// vector table, the reset code, the C runtime and a card driver the application calls. This builds our own
	// replacement: the same entry points, a driver that never sees a card, no updater.
	// Returns the bootable 1 MB image, or nothing if the application does not call the entry points we provide.
	std::vector<uint8_t> createRackBootImage(const std::vector<uint8_t>& _firmware);
}
