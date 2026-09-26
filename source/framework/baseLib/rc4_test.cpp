#include "rc4.h"
#include "os.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// baseLib::rc4 against published vectors: the three of the RC4 article on Wikipedia and the first 32 key stream bytes
// of RFC 6229 for the 40 bit key 01 02 03 04 05

int main()
{
	baseLib::disableErrorDialogs();

	int checks = 0;
	int failures = 0;

	using Bytes = std::vector<uint8_t>;

	auto check = [&](const char* _what, const Bytes& _key, Bytes _data, const Bytes& _expected)
	{
		++checks;
		baseLib::rc4(_data.data(), _data.size(), _key.data(), _key.size());
		if(_data == _expected)
			return;
		++failures;
		printf("FAILED %s\n", _what);
	};

	auto bytes = [](const std::string& _s) { return Bytes(_s.begin(), _s.end()); };

	check("Key", bytes("Key"), bytes("Plaintext"), {0xbb, 0xf3, 0x16, 0xe8, 0xd9, 0x40, 0xaf, 0x0a, 0xd3});
	check("Wiki", bytes("Wiki"), bytes("pedia"), {0x10, 0x21, 0xbf, 0x04, 0x20});
	check("Secret", bytes("Secret"), bytes("Attack at dawn"), {0x45, 0xa0, 0x1f, 0x64, 0x5f, 0xc3, 0x5b, 0x38, 0x35, 0x52, 0x54, 0x4b, 0x9b, 0xf5});
	check("RFC 6229", {1, 2, 3, 4, 5}, Bytes(32, 0), {
		0xb2, 0x39, 0x63, 0x05, 0xf0, 0x3d, 0xc0, 0x27, 0xcc, 0xc3, 0x52, 0x4a, 0x0a, 0x11, 0x18, 0xa8,
		0x69, 0x82, 0x94, 0x4f, 0x18, 0xfc, 0x82, 0xd5, 0x89, 0xc4, 0x03, 0xa4, 0x7a, 0x0d, 0x09, 0x19});

	// the same key stream decrypts
	check("decrypt", bytes("Key"), {0xbb, 0xf3, 0x16, 0xe8, 0xd9, 0x40, 0xaf, 0x0a, 0xd3}, bytes("Plaintext"));

	check("empty key", {}, bytes("data"), bytes("data"));

	printf("%d of %d checks failed\n", failures, checks);
	return failures ? 1 : 0;
}
