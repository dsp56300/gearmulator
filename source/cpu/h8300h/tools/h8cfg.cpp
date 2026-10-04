#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "cpu/h8300h/decode.hpp"

namespace
{
	struct Instruction
	{
		uint32_t address = 0;
		uint32_t length = 0;
		std::string kind = "flow";
		uint32_t target = 0;
		bool hasTarget = false;
	};

	uint32_t readBe32(const std::vector<uint8_t>& _rom, const uint32_t _address)
	{
		if (_address + 4 > _rom.size())
			return 0xffffffff;
		return (static_cast<uint32_t>(_rom[_address]) << 24) |
			(static_cast<uint32_t>(_rom[_address + 1]) << 16) |
			(static_cast<uint32_t>(_rom[_address + 2]) << 8) |
			static_cast<uint32_t>(_rom[_address + 3]);
	}

	uint32_t readBe24(const std::vector<uint8_t>& _rom, const uint32_t _address)
	{
		if (_address + 3 > _rom.size())
			return 0xffffffff;
		return (static_cast<uint32_t>(_rom[_address]) << 16) |
			(static_cast<uint32_t>(_rom[_address + 1]) << 8) |
			static_cast<uint32_t>(_rom[_address + 2]);
	}

	int16_t readBeS16(const std::vector<uint8_t>& _rom, const uint32_t _address)
	{
		return static_cast<int16_t>((static_cast<uint16_t>(_rom[_address]) << 8) | _rom[_address + 1]);
	}

	std::string hexAddress(const uint32_t _address)
	{
		std::ostringstream result;
		result << "0x" << std::hex << std::setw(6) << std::setfill('0') << _address;
		return result.str();
	}

	std::string hexBytes(const std::vector<uint8_t>& _rom, const uint32_t _address, const uint32_t _length)
	{
		std::ostringstream result;
		result << std::hex << std::setfill('0');
		for (uint32_t i = 0; i < _length; ++i)
		{
			if (i)
				result << ' ';
			result << std::setw(2) << static_cast<uint32_t>(_rom[_address + i]);
		}
		return result.str();
	}
}

int main(int _argc, char** _argv)
{
	if (_argc != 3 && _argc != 4)
	{
		std::cerr << "Usage: h8cfg <rom.bin> <output-directory> [seed-addresses.txt]\n";
		return 2;
	}

	std::ifstream input(_argv[1], std::ios::binary);
	if (!input)
	{
		std::cerr << "Unable to open " << _argv[1] << '\n';
		return 1;
	}
	const std::vector<uint8_t> rom(std::istreambuf_iterator<char>(input), {});
	const std::string outputDirectory = _argv[2];
	std::filesystem::create_directories(outputDirectory);

	auto instructionLength = [&rom](const uint32_t _address) -> uint32_t
	{
		const auto insn = h8300h::decode([&rom, _address](const unsigned _i)
		{
			const auto at = _address + _i;
			return at < rom.size() ? rom[at] : uint8_t(0xff);
		});
		return insn.valid() ? insn.len : 0u;
	};

	std::queue<uint32_t> pending;
	std::set<uint32_t> queued;
	std::set<uint32_t> functions;
	std::map<uint32_t, Instruction> instructions;
	std::vector<std::tuple<uint32_t, uint32_t, std::string>> xrefs;

	auto enqueue = [&](const uint32_t _address)
	{
		if (_address < rom.size() && (_address & 1) == 0 && queued.insert(_address).second)
			pending.push(_address);
	};
	auto addFunction = [&](const uint32_t _address)
	{
		if (_address < rom.size() && (_address & 1) == 0)
		{
			functions.insert(_address);
			enqueue(_address);
		}
	};
	auto addTarget = [&](const uint32_t _source, const uint32_t _target, const std::string& _kind, const bool _function)
	{
		xrefs.emplace_back(_source, _target, _kind);
		if (_function)
			addFunction(_target);
		else
			enqueue(_target);
	};

	for (uint32_t vector = 0; vector < 64; ++vector)
	{
		const auto target = readBe32(rom, vector * 4) & 0xffffff;
		if (!target || target >= rom.size())
			continue;
		addFunction(target);
		xrefs.emplace_back(vector * 4, target, "vector");
	}
	if (_argc == 4)
	{
		std::ifstream seedInput(_argv[3]);
		std::string line;
		while (std::getline(seedInput, line))
		{
			const auto separator = line.find_first_of(", \t");
			const auto token = line.substr(0, separator);
			char* end = nullptr;
			errno = 0;
			const auto address = std::strtoul(token.c_str(), &end, 0);
			if (!errno && end != token.c_str() && *end == '\0')
				addFunction(static_cast<uint32_t>(address));
		}
	}

	while (!pending.empty())
	{
		const auto address = pending.front();
		pending.pop();
		if (instructions.find(address) != instructions.end() || address + 2 > rom.size())
			continue;

		const auto length = instructionLength(address);
		const auto next = address + length;
		if (!length || next > rom.size())
			continue;

		Instruction instruction;
		instruction.address = address;
		instruction.length = next - address;
		const auto opcode = rom[address];

		if ((opcode & 0xf0) == 0x40)
		{
			instruction.kind = opcode == 0x40 ? "branch_always" : (opcode == 0x41 ? "branch_never" : "branch");
			instruction.target = static_cast<uint32_t>(next + static_cast<int8_t>(rom[address + 1])) & 0xffffff;
			instruction.hasTarget = true;
			addTarget(address, instruction.target, instruction.kind, false);
			if (opcode != 0x40)
				enqueue(next);
		}
		else if (opcode == 0x58 && instruction.length == 4)
		{
			const auto condition = rom[address + 1] >> 4;
			instruction.kind = condition == 0 ? "branch_always" : (condition == 1 ? "branch_never" : "branch");
			instruction.target = static_cast<uint32_t>(next + readBeS16(rom, address + 2)) & 0xffffff;
			instruction.hasTarget = true;
			addTarget(address, instruction.target, instruction.kind, false);
			if (condition != 0)
				enqueue(next);
		}
		else if (opcode == 0x55 || (opcode == 0x5c && instruction.length == 4))
		{
			instruction.kind = "call";
			instruction.target = opcode == 0x55
				? static_cast<uint32_t>(next + static_cast<int8_t>(rom[address + 1])) & 0xffffff
				: static_cast<uint32_t>(next + readBeS16(rom, address + 2)) & 0xffffff;
			instruction.hasTarget = true;
			addTarget(address, instruction.target, "call", true);
			enqueue(next);
		}
		else if (opcode == 0x5e && instruction.length == 4)
		{
			instruction.kind = "call";
			instruction.target = readBe24(rom, address + 1);
			instruction.hasTarget = true;
			addTarget(address, instruction.target, "call", true);
			enqueue(next);
		}
		else if (opcode == 0x5f && instruction.length == 2)
		{
			instruction.kind = "call_vector";
			instruction.target = readBe32(rom, rom[address + 1] & 0xfe) & 0xffffff;
			instruction.hasTarget = true;
			addTarget(address, instruction.target, "call_vector", true);
			enqueue(next);
		}
		else if (opcode == 0x5d && instruction.length == 2)
		{
			instruction.kind = "call_indirect";
			enqueue(next);
		}
		else if (opcode == 0x5a && instruction.length == 4)
		{
			instruction.kind = "jump";
			instruction.target = readBe24(rom, address + 1);
			instruction.hasTarget = true;
			addTarget(address, instruction.target, "jump", false);
		}
		else if (opcode == 0x5b && instruction.length == 2)
		{
			instruction.kind = "jump_vector";
			instruction.target = readBe32(rom, rom[address + 1] & 0xfe) & 0xffffff;
			instruction.hasTarget = true;
			addTarget(address, instruction.target, "jump_vector", false);
		}
		else if (opcode == 0x59 && instruction.length == 2)
		{
			instruction.kind = "jump_indirect";
		}
		else if ((opcode == 0x54 || opcode == 0x56) && instruction.length == 2)
		{
			instruction.kind = opcode == 0x54 ? "return" : "interrupt_return";
		}
		else if (opcode == 0x57 && instruction.length == 2)
		{
			instruction.kind = "trap";
			const auto trapVectorAddress = 0x20 + ((rom[address + 1] >> 4) * 4);
			instruction.target = readBe32(rom, trapVectorAddress) & 0xffffff;
			instruction.hasTarget = true;
			addTarget(address, instruction.target, "trap", true);
			enqueue(next);
		}
		else
		{
			enqueue(next);
		}

		instructions.emplace(address, instruction);
	}

	std::map<uint32_t, uint32_t> callCounts;
	for (const auto& [source, target, kind] : xrefs)
		if (kind == "call" || kind == "call_vector" || kind == "trap" || kind == "vector")
			++callCounts[target];

	std::ofstream instructionOutput(outputDirectory + "/instructions.csv");
	instructionOutput << "address,length,bytes,kind,target\n";
	for (const auto& [address, instruction] : instructions)
	{
		instructionOutput << hexAddress(address) << ',' << instruction.length << ",\""
			<< hexBytes(rom, address, instruction.length) << "\"," << instruction.kind << ',';
		if (instruction.hasTarget)
			instructionOutput << hexAddress(instruction.target);
		instructionOutput << '\n';
	}

	std::ofstream functionOutput(outputDirectory + "/functions.csv");
	functionOutput << "address,name,direct_refs\n";
	for (const auto address : functions)
		functionOutput << hexAddress(address) << ",sub_" << hexAddress(address).substr(2) << ',' << callCounts[address] << '\n';

	std::ofstream xrefOutput(outputDirectory + "/xrefs.csv");
	xrefOutput << "source,target,kind\n";
	for (const auto& [source, target, kind] : xrefs)
		xrefOutput << hexAddress(source) << ',' << hexAddress(target) << ',' << kind << '\n';

	std::cout << "Reachable instructions: " << instructions.size() << '\n'
		<< "Function entries: " << functions.size() << '\n'
		<< "Control-flow xrefs: " << xrefs.size() << '\n';
	return 0;
}
