#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "cpu/h8300h/decode.hpp"
#include "cpu/h8300h/disasm.hpp"

namespace
{
	uint32_t parseNumber(const char* _value)
	{
		char* end = nullptr;
		errno = 0;
		const auto result = std::strtoul(_value, &end, 0);
		if (errno || end == _value || *end != '\0' || result > 0xffffff)
			throw std::runtime_error(std::string("Invalid address/count: ") + _value);
		return static_cast<uint32_t>(result);
	}
}

int main(int _argc, char** _argv)
{
	if (_argc != 4)
	{
		std::cerr << "Usage: h8dasm <rom.bin> <start> <byte-count>\n";
		return 2;
	}

	try
	{
		std::ifstream input(_argv[1], std::ios::binary);
		if (!input)
			throw std::runtime_error(std::string("Unable to open ") + _argv[1]);

		const std::vector<uint8_t> rom(std::istreambuf_iterator<char>(input), {});
		const auto start = parseNumber(_argv[2]);
		const auto byteCount = parseNumber(_argv[3]);
		const auto end = std::min<uint32_t>(start + byteCount, static_cast<uint32_t>(rom.size()));

		for (auto address = start; address < end;)
		{
			const auto insn = h8300h::decode([&rom, address](const unsigned _i)
			{
				const auto at = address + _i;
				return at < rom.size() ? rom[at] : uint8_t(0xff);
			});
			const auto length = insn.valid() ? insn.len : 2u;
			std::printf("%06x:\t%-20s\t%s\n", address, h8300h::format_bytes(rom.data() + address, length).c_str(),
				h8300h::disassemble(insn, address + length).c_str());
			address += length;
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}

	return 0;
}
