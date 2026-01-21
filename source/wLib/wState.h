#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>

#include "synthLib/midiTypes.h"

namespace wLib
{
	using SysEx = synthLib::SysexBuffer;
	using Responses = synthLib::SysexBufferList;

	class State
	{
	protected:
		template<size_t Size> static bool convertTo(std::array<uint8_t, Size>& _dst, const SysEx& _data)
		{
			if(_data.size() != Size)
				return false;
			std::copy(_data.begin(), _data.end(), _dst.begin());
			return true;
		}

		template<size_t Size> static SysEx convertTo(const std::array<uint8_t, Size>& _src)
		{
			SysEx dst;
			dst.insert(dst.begin(), _src.begin(), _src.end());
			return dst;
		}

		template<size_t Size> static void updateChecksum(std::array<uint8_t, Size>& _src, uint32_t _startIndex)
		{
			uint8_t& c = _src[_src.size() - 2];
			c = 0;
			for(size_t i = _startIndex; i<_src.size()-2; ++i)
				c += _src[i];
			c &= 0x7f;
		}

	};
}
