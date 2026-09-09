#include "nmmrom.h"
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace nmm
{
	// SHA-256, using unsigned arithmetic as specified by FIPS 180-4.
	std::string sha256(const std::vector<uint8_t>& data)
	{
		constexpr uint32_t k[] = {
			0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
			0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
			0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
			0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
			0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
			0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
			0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
			0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
		std::array<uint32_t,8> h{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
		auto bytes = data;
		bytes.push_back(0x80);
		while(bytes.size() % 64 != 56) bytes.push_back(0);
		const auto bits = static_cast<uint64_t>(data.size()) * 8;
		for(int i=7; i>=0; --i) bytes.push_back(static_cast<uint8_t>(bits >> (8*i)));
		auto r = [](uint32_t x, unsigned n) { return (x >> n) | (x << (32-n)); };
		for(size_t offset=0; offset<bytes.size(); offset+=64)
		{
			uint32_t w[64]{};
			for(unsigned i=0; i<16; ++i)
				for(unsigned j=0; j<4; ++j) w[i] = (w[i]<<8) | bytes[offset+4*i+j];
			for(unsigned i=16; i<64; ++i)
				w[i] = w[i-16] + (r(w[i-15],7)^r(w[i-15],18)^(w[i-15]>>3)) + w[i-7] + (r(w[i-2],17)^r(w[i-2],19)^(w[i-2]>>10));
			auto v = h;
			for(unsigned i=0; i<64; ++i)
			{
				const auto t1 = v[7] + (r(v[4],6)^r(v[4],11)^r(v[4],25)) + ((v[4]&v[5])^(~v[4]&v[6])) + k[i] + w[i];
				const auto t2 = (r(v[0],2)^r(v[0],13)^r(v[0],22)) + ((v[0]&v[1])^(v[0]&v[2])^(v[1]&v[2]));
				for(unsigned j=7; j>0; --j) v[j]=v[j-1];
				v[4]+=t1; v[0]=t1+t2;
			}
			for(unsigned i=0; i<8; ++i) h[i]+=v[i];
		}
		std::ostringstream out;
		for(auto v:h) out << std::hex << std::setfill('0') << std::setw(8) << v;
		return out.str();
	}

	Rom::Rom(const std::string& filename)
	{
		std::ifstream input(filename, std::ios::binary | std::ios::ate);
		if(!input) throw std::runtime_error("Cannot open firmware: " + filename);
		if(input.tellg() != Size) throw std::runtime_error("Expected decoded Micro Modular v3.03b firmware (354016 bytes)");
		m_data.resize(Size);
		input.seekg(0);
		if(!input.read(reinterpret_cast<char*>(m_data.data()), Size)) throw std::runtime_error("Cannot read firmware");
		if(sha256(m_data) != "0ccbffa696f4aa65baa53768695dc02b3afa61ce572006b532cf278515759c3c")
			throw std::runtime_error("Unsupported firmware SHA-256; expected decoded Micro Modular v3.03b");
	}
	uint32_t Rom::word(uint32_t offset) const
	{
		if(offset > Size-4) throw std::out_of_range("DSP source word outside firmware");
		uint32_t result=0;
		for(unsigned i=0;i<4;++i) result = (result<<8) | m_data[offset+i];
		if(result>0xffffff) throw std::runtime_error("DSP source word exceeds 24 bits");
		return result;
	}
	std::vector<uint32_t> Rom::resident() const
	{
		std::vector<uint32_t> result(0x205,0);
		for(unsigned i=0;i<0x175;++i) result[i]=word(0x3d87e + 4*i);
		for(unsigned i=0;i<5;++i) result[0x200+i]=word(0x3de52+4*i);
		return result;
	}
}
