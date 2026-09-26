#include "lzo1x.h"
#include "os.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// baseLib::lzo1xDecompress against hand built LZO1X streams, one per instruction type of the format, and malformed
// ones. Every stream ends with the end marker 11 00 00.

namespace
{
	int g_checks = 0;
	int g_failures = 0;

	void check(const bool _condition, const char* _test, const char* _what)
	{
		++g_checks;
		if(_condition)
			return;
		++g_failures;
		printf("FAILED %s: %s\n", _test, _what);
	}

	#define CHECK(_test, _condition) check((_condition), _test, #_condition)

	using Bytes = std::vector<uint8_t>;

	Bytes bytes(const std::string& _s)
	{
		return {_s.begin(), _s.end()};
	}

	// "xyz" repeated to _size bytes
	Bytes pattern(const size_t _size)
	{
		Bytes b;
		for(size_t i=0; i<_size; ++i)
			b.push_back(static_cast<uint8_t>("xyz"[i % 3]));
		return b;
	}

	Bytes cat(Bytes _a, const Bytes& _b)
	{
		_a.insert(_a.end(), _b.begin(), _b.end());
		return _a;
	}

	bool decodes(const Bytes& _src, const Bytes& _expected)
	{
		Bytes out;
		return baseLib::lzo1xDecompress(_src, out) && out == _expected;
	}

	bool fails(const Bytes& _src)
	{
		Bytes out;
		return !baseLib::lzo1xDecompress(_src, out);
	}

	// 14 = 17 + 3 literals "xyz", then an M3 match at distance 3 (08 00) whose length continues in _zeros zero bytes and
	// _last: 31 + 255 * _zeros + _last, plus 2
	Bytes seededRun(const size_t _zeros, const uint8_t _last)
	{
		Bytes s = {0x14, 'x', 'y', 'z', 0x20};
		s.insert(s.end(), _zeros, 0x00);
		s.push_back(_last);
		s.push_back(0x08);
		s.push_back(0x00);
		return s;
	}

	const Bytes g_end = {0x11, 0x00, 0x00};

	void testFirstLiteralRun()
	{
		// 15 = 17 + 4 literals
		CHECK("firstLiteralRun", decodes(cat({0x15, 'a', 'b', 'c', 'd'}, g_end), bytes("abcd")));
	}

	void testM2()
	{
		// A8: length (A8 >> 5) + 1 = 6, distance 1 + ((A8 >> 2) & 7) + (00 << 3) = 3
		CHECK("M2", decodes(cat({0x14, 'a', 'b', 'c', 0xa8, 0x00}, g_end), bytes("abcabcabc")));

		// A9: the same match, its two low bits say that one literal follows
		CHECK("M2 + literal", decodes(cat({0x14, 'a', 'b', 'c', 0xa9, 0x00, 'Z'}, g_end), bytes("abcabcabcZ")));
	}

	void testM3()
	{
		// 25 = 32 + length 5, plus 2 = 7, distance 1 + (08 >> 2) + (00 << 6) = 3
		CHECK("M3", decodes(cat({0x14, 'x', 'y', 'z', 0x25, 0x08, 0x00}, g_end), bytes("xyzxyzxyzx")));

		// length 0 continues: 255 per zero byte, then 31 + 5, plus 2 = 293
		CHECK("M3 long", decodes(cat(seededRun(1, 0x05), g_end), pattern(3 + 293)));
	}

	void testLiteralRunAfterMatch()
	{
		// after a match with 0 in its low bits, 02 is a run of 2 + 3 literals
		CHECK("literal run", decodes(cat({0x14, 'a', 'b', 'c', 0xa8, 0x00, 0x02, '1', '2', '3', '4', '5'}, g_end), bytes("abcabcabc12345")));
	}

	void testM1AfterLiteralRun()
	{
		// 2100 bytes (31 + 255 * 8 + 24 + 2 = 2097 after the seed), then 01 = 1 + 3 literals, then 00 00 right after a
		// literal run: 3 bytes at distance 1 + 0x800 + (00 >> 2) + (00 << 2) = 2049
		const auto src = cat(cat(seededRun(8, 24), {0x01, 'A', 'B', 'C', 'D', 0x00, 0x00}), g_end);

		auto expected = cat(pattern(2100), bytes("ABCD"));
		const auto from = expected.size() - 2049;
		expected.insert(expected.end(), {expected[from], expected[from + 1], expected[from + 2]});

		CHECK("M1", decodes(src, expected));
	}

	void testM4()
	{
		// 17000 bytes (31 + 255 * 66 + 134 + 2 = 16997 after the seed), then 11 = 16 + length 1, plus 2 = 3, distance
		// 0x4000 + (04 >> 2) + (00 << 6) = 16385
		const auto src = cat(cat(seededRun(66, 134), {0x11, 0x04, 0x00}), g_end);

		auto expected = pattern(17000);
		const auto from = expected.size() - 16385;
		expected.insert(expected.end(), {expected[from], expected[from + 1], expected[from + 2]});

		CHECK("M4", decodes(src, expected));
	}

	void testMalformed()
	{
		CHECK("empty", fails({}));
		CHECK("no end marker", fails({0x15, 'a', 'b', 'c', 'd'}));
		CHECK("truncated literals", fails({0x15, 'a', 'b'}));
		CHECK("distance before the start", fails(cat({0x14, 'a', 'b', 'c', 0xa8, 0x01}, g_end)));
		CHECK("bytes after the end marker", fails(cat(cat({0x15, 'a', 'b', 'c', 'd'}, g_end), {0x00})));
		CHECK("end marker with a length", fails({0x15, 'a', 'b', 'c', 'd', 0x12, 0x00, 0x00}));
	}
}

int main()
{
	baseLib::disableErrorDialogs();

	testFirstLiteralRun();
	testM2();
	testM3();
	testLiteralRunAfterMatch();
	testM1AfterLiteralRun();
	testM4();
	testMalformed();

	printf("%d of %d checks failed\n", g_failures, g_checks);
	return g_failures ? 1 : 0;
}
