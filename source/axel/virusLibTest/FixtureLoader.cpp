#include "FixtureLoader.h"

#include <cctype>
#include <fstream>
#include <stdexcept>

namespace virusLibTest
{
	namespace
	{
		uint8_t decodeNibble(const char _value)
		{
			if(_value >= '0' && _value <= '9')
				return static_cast<uint8_t>(_value - '0');
			if(_value >= 'A' && _value <= 'F')
				return static_cast<uint8_t>(_value - 'A' + 10);
			if(_value >= 'a' && _value <= 'f')
				return static_cast<uint8_t>(_value - 'a' + 10);
			throw std::runtime_error("Invalid hexadecimal fixture data");
		}
	}

	synthLib::SysexBuffer loadBinaryFixture(const std::string& _filename)
	{
		const std::string path = std::string(VIRUSLIB_TEST_DATA_DIR) + '/' + _filename;
		std::ifstream stream(path, std::ios::binary);
		if(!stream)
			throw std::runtime_error("Unable to open fixture: " + path);

		return synthLib::SysexBuffer(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
	}

	synthLib::SysexBuffer loadHexFixture(const std::string& _filename)
	{
		const std::string path = std::string(VIRUSLIB_TEST_DATA_DIR) + '/' + _filename;
		std::ifstream stream(path);
		if(!stream)
			throw std::runtime_error("Unable to open fixture: " + path);

		synthLib::SysexBuffer result;
		char high = 0;
		char value = 0;
		while(stream.get(value))
		{
			if(std::isspace(static_cast<unsigned char>(value)))
				continue;

			if(!high)
			{
				high = value;
				continue;
			}

			result.push_back(static_cast<uint8_t>((decodeNibble(high) << 4) | decodeNibble(value)));
			high = 0;
		}

		if(high)
			throw std::runtime_error("Fixture contains an incomplete hexadecimal byte: " + path);
		return result;
	}
}
