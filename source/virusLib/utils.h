#pragma once

#include <cstdint>
#include <iostream>
#include <vector>


namespace virusLib
{
	static uint16_t swap16(const uint32_t _val)
	{
		return (_val & 0xff) << 8 | (_val & 0xff00) >> 8;
	}

	static uint32_t swap32(const uint32_t _val)
	{
		return ((_val & 0xff) << 24) | ((_val & 0xff00) << 8) | ((_val & 0xff0000) >> 8) | (_val >> 24);
	}

	struct membuf : std::streambuf
	{
		explicit membuf(const std::vector<char>& base)
		{
			char* p(const_cast<char*>(base.data()));
			this->setg(p, p, p + base.size());
		}

	protected:
		pos_type seekpos(pos_type sp, std::ios_base::openmode which) override
		{
			return seekoff(sp - pos_type(off_type(0)), std::ios_base::beg, which);
		}

		pos_type seekoff(off_type off,
		                 std::ios_base::seekdir dir,
		                 std::ios_base::openmode which = std::ios_base::in) override
		{
			if (dir == std::ios_base::cur)
				gbump(off);
			else if (dir == std::ios_base::end)
				setg(eback(), egptr() + off, egptr());
			else if (dir == std::ios_base::beg)
				setg(eback(), eback() + off, egptr());
			return gptr() - eback();
		}
	};

	struct imemstream : virtual membuf, std::istream
	{
		explicit imemstream(const std::vector<char>& base)
				: membuf(base), std::istream(static_cast<std::streambuf*>(this))
		{
		}
	};
}
