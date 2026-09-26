#include "adler32.h"
#include "os.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// baseLib::adler32 against the values of zlib's adler32

int main()
{
	baseLib::disableErrorDialogs();

	int checks = 0;
	int failures = 0;

	using Bytes = std::vector<uint8_t>;

	auto check = [&](const char* _what, const Bytes& _data, const uint32_t _expected)
	{
		++checks;
		const auto sum = baseLib::adler32(_data.data(), _data.size());
		if(sum == _expected)
			return;
		++failures;
		printf("FAILED %s: %08x instead of %08x\n", _what, sum, _expected);
	};

	auto bytes = [](const std::string& _s) { return Bytes(_s.begin(), _s.end()); };

	check("empty", {}, 0x00000001);
	check("a", bytes("a"), 0x00620062);
	check("abc", bytes("abc"), 0x024d0127);
	check("Wikipedia", bytes("Wikipedia"), 0x11e60398);
	check("100000 x ff", Bytes(100000, 0xff), 0x149a302c);

	Bytes ramp(1000000);
	for(size_t i=0; i<ramp.size(); ++i)
		ramp[i] = static_cast<uint8_t>(i);
	check("1000000 byte ramp", ramp, 0x0e27d8d8);

	printf("%d of %d checks failed\n", failures, checks);
	return failures ? 1 : 0;
}
