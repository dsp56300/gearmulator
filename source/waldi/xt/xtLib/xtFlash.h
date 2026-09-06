#pragma once

#include "hardwareLib/am29f.h"

namespace xt
{
	// XT uses two times AM29F010 which is 128k * 8.
	// However, they are connected in interleaved mode, forming one 256k * 8
	// For this to work, we invent a 2mbit flash without boot block that can do sector erases in 16k blocks as
	// that is what is used in the XT
	class Flash final : public hwLib::Am29f
	{
	public:
		explicit Flash(uint8_t* _buffer, size_t _size, bool _useWriteEnable, bool _bitreversedCmdAddr);

		bool eraseSector(uint32_t _addr) const override;
	};
}
