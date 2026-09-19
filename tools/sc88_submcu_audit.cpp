// Execute unmodified control-ROM mailbox handlers in a controlled H8 fixture.
// This is an interface probe, not a whole-board or physical-hardware test.
// Build: c++ -std=c++20 -O2 -Isource -Isource/cpu tools/sc88_submcu_audit.cpp \
//        build/source/cpu/h8500/libh8500.a -o /tmp/sc88_submcu_audit
#include "cpu/h8500/cpu.hpp"
#include "cpu/h8500/disasm.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <iterator>
#include <stdexcept>

struct Fixture
{
    h8500::Bus bus;
    h8500::Cpu cpu{bus, h8500::make_chip_config(h8500::ChipModel::H8_510, 4)};
    bool pro;
    unsigned ram, sm;
    Fixture(const std::vector<uint8_t>& rom, bool p) : pro(p), ram(p ? 0xc00000 : 0x080000), sm(p ? 0xe00000 : 0x0f0000)
    {
        bus.map_ram(0, bus.size(), h8500::BusClass::W16_S3);
        std::fill(bus.mem(), bus.mem() + bus.size(), 0);
        bus.load(0, rom.data(), rom.size());
        cpu.reset();
        auto& r = cpu.regs();
        r.cp = 0; r.dp = r.ep = r.tp = ram >> 16; r.r[7] = 0xf000;
        bus.write16(ram + (pro ? 0x078a : 0x06ee), 0x0010);
        bus.write8(sm + 0xd0, 0x10); bus.write8(sm + 0xd1, 0x10);
    }
    unsigned pc() const { return cpu.code_addr(cpu.regs().pc); }
    void runTo(unsigned end)
    {
        for(unsigned n = 0; n < 10000; ++n)
        {
            if(pc() == end) return;
            cpu.step();
        }
        throw std::runtime_error("did not reach stop PC " + std::to_string(end) + "; PC=" + std::to_string(pc()));
    }
    void mailbox(uint8_t command, uint8_t channel, uint8_t p1, uint8_t p2, std::initializer_list<uint8_t> data)
    {
        unsigned pos = sm + 0x14;
        for(auto b : data) bus.write8(pos++, b);
        bus.write8(sm + 0xdc, command); bus.write8(sm + 0xdd, channel);
        bus.write8(sm + 0xde, p1); bus.write8(sm + 0xdf, p2);
        cpu.regs().pc = pro ? 0x0bb9 : 0x0bdd;
        runTo(0x0c4b);
    }
    void expect(const char* name, unsigned op, unsigned params)
    {
        const auto& r = cpu.regs();
        std::cout << name << ": queued=" << std::hex << std::setw(4) << std::setfill('0') << r.r[0]
                  << "/" << std::setw(4) << r.r[1] << " ack=" << unsigned(bus.read8(sm + 0xfd)) << '\n';
        if(r.r[0] != op || r.r[1] != params) throw std::runtime_error("unexpected firmware result");
    }
};

int main(int argc, char** argv)
{
    try
    {
        if(argc < 3) throw std::runtime_error("usage: sc88_submcu_audit ROM sc88|sc88pro [--tables | address instruction-count]");
        std::ifstream in(argv[1], std::ios::binary);
        if(!in) throw std::runtime_error("cannot open ROM");
        std::vector<uint8_t> rom{std::istreambuf_iterator<char>(in), {}};
        bool pro = std::string(argv[2]) == "sc88pro";
        if(!pro && std::string(argv[2]) != "sc88") throw std::runtime_error("unknown model");
        if(rom.size() != (pro ? 0x100000u : 0x80000u)) throw std::runtime_error("wrong ROM size");
        if(argc == 5)
        {
            unsigned pc = std::stoul(argv[3], nullptr, 16), count = std::stoul(argv[4]);
            for(unsigned n = 0; n < count; ++n)
            {
                if(pc >= rom.size()) throw std::runtime_error("disassembly address outside ROM");
                const auto ins = h8500::decode([&](unsigned off) { return pc + off < rom.size() ? rom[pc + off] : uint8_t(0xff); });
                std::cout << std::hex << std::setw(6) << std::setfill('0') << pc << "  "
                          << h8500::format_bytes(rom.data() + pc, std::min<size_t>(ins.length, rom.size() - pc)) << "  "
                          << h8500::disassemble(ins, pc + ins.length) << '\n';
                pc += ins.length ? ins.length : 1;
                if(pc >= rom.size()) break;
            }
            return 0;
        }
        uint64_t fingerprint = 0xcbf29ce484222325ULL;
        for(auto byte : rom) fingerprint = (fingerprint ^ byte) * 0x100000001b3ULL;
        if(fingerprint != (pro ? 0x710de3958ee71769ULL : 0x51935d7b82687c72ULL))
            throw std::runtime_error("probe offsets are not verified for this ROM; disassembly mode remains available");
        if(argc == 4 && std::string(argv[3]) == "--tables")
        {
            const unsigned base = pro ? 0x10ee4 : 0x10b6c;
            for(unsigned op = 0; op < 0x100; ++op)
            {
                unsigned off = base - 32 + op * 2;
                std::cout << std::hex << std::setfill('0') << std::setw(2) << op
                          << " set=" << std::setw(4) << ((rom[off] << 8) | rom[off + 1]);
                if(op >= 0x10) std::cout << " get=" << std::setw(4) << ((rom[off + 0x1e0] << 8) | rom[off + 0x1e1]);
                std::cout << '\n';
            }
            return 0;
        }
        // A compact RQ1: 40 00 04, size 00 00 01. The first six bytes are
        // wire manufacturer/device/model/command/address-high/address-middle.
        {
            Fixture f(rom, pro);
            f.mailbox(0x60, 0x80, 0x8b, 0x14, {0x41,0x10,0x42,0x11,0x40,0,4,0,0,1,0x3b});
            f.expect("RQ1 40 00 04 size 1", 0x0060, 0x8401);
            const auto message = f.ram + (pro ? 0xcf68 : 0xdf60);
            f.bus.write8(message, 0); f.bus.write8(message + 1, 0x60);
            f.bus.write8(message + 2, 0x84); f.bus.write8(message + 3, 1);
            f.bus.write8(f.ram + (pro ? 0x5042 : 0x8042), 73);
            f.cpu.regs().ep = 1; // MIDI consumer keeps its descriptor tables in page 1.
            f.cpu.regs().pc = pro ? 0x17c7 : 0x15ae;
            f.runTo(pro ? 0x1938 : 0x16e0);
            if(f.bus.read8(message + 2) != 4 || f.bus.read8(message + 3) != 73)
                throw std::runtime_error("firmware did not read master volume for reply");
            std::cout << "RQ1 firmware reply descriptor: address-low=04 data=49 (seeded master volume)\n";
        }
        {
            Fixture f(rom, pro);
            f.mailbox(0x60, 0x90, 0x8b, 0x14, {0x41,0x10,0x42,0x11,0x40,0,4,0,1,0,0x3b});
            f.expect("RQ1 source B size 128", 0x1060, 0x8480);
        }
        {
            Fixture f(rom, pro);
            const auto message = f.ram + (pro ? 0xcf68 : 0xdf60);
            // Compact DT1 for 00 01 10 = 0. One part, not an entire group.
            f.bus.write8(message, 1); f.bus.write8(message + 1, 0x20);
            f.bus.write8(message + 2, 0x10); f.bus.write8(message + 3, 0);
            const auto assignments = f.ram + (pro ? 0x5020 : 0x8020);
            const auto channels = f.ram + (pro ? 0xca20 : 0xda20);
            f.bus.write8(assignments + 16, 1); f.bus.write8(assignments + 17, 1);
            f.bus.write8(channels + 16, 0x13);
            f.cpu.regs().pc = pro ? 0x17c7 : 0x15ae;
            f.runTo(pro ? 0x0138cf : 0x012b70);
            if(f.bus.read8(assignments + 16) != 0 || f.bus.read8(assignments + 17) != 1)
                throw std::runtime_error("firmware per-part routing update failed");
            std::cout << "RX PORT firmware changes block 10 only; block 11 remains on B\n";
        }
        {
            Fixture f(rom, pro);
            f.mailbox(0xee, 0x80, 4, 0x14, {0x7e,0x7f,9,1});
            f.expect("GM staged", 0x00ee, pro ? 0x0109 : 0x0100);
        }
        if(pro)
        {
            Fixture f(rom, pro);
            f.mailbox(0xee, 0x80, 4, 0x14, {0x7e,0x7f,6,1});
            f.expect("Identity staged", 0x00ee, 0x0106);
            // Execute the firmware's own identity-template initialization.
            f.cpu.regs().dp = 0xc0; f.cpu.regs().cp = 1; f.cpu.regs().pc = 0x0da4;
            f.runTo(0x010dde);
            // Feed the firmware's decoded result into its universal handler.
            f.bus.write8(f.ram + 0xcf6a, 1); f.bus.write8(f.ram + 0xcf6b, 6);
            f.cpu.regs().dp = f.cpu.regs().ep = 0xc0;
            f.cpu.regs().cp = 0; f.cpu.regs().pc = 0x1ac8;
            f.runTo(0x010c58);
            std::cout << "Identity reached firmware transmitter 01:0c58, buffer=" << std::hex
                      << f.cpu.regs().r[4] << " length-minus-one=" << f.cpu.regs().r[3] << '\n';
            std::cout << "Identity firmware buffer:";
            for(unsigned i = 0; i < 15; ++i) std::cout << ' ' << std::setw(2) << unsigned(f.bus.read8(f.ram + 0xd00a + i));
            std::cout << '\n';
            // Negative control: the production parser currently drops sub-ID1.
            Fixture missingSubId(rom, true);
            missingSubId.bus.write8(missingSubId.ram + 0xcf69, 0xef);
            missingSubId.bus.write8(missingSubId.ram + 0xcf6a, 85);
            missingSubId.bus.write8(missingSubId.ram + 0x5042, 73);
            missingSubId.cpu.regs().ep = 1; missingSubId.cpu.regs().pc = 0x17c7;
            missingSubId.runTo(0x17b1);
            if(missingSubId.bus.read8(missingSubId.ram + 0x5042) != 73)
                throw std::runtime_error("negative control unexpectedly changed volume");
            Fixture withSubId(rom, true);
            withSubId.bus.write8(withSubId.ram + 0xcf69, 0xef);
            withSubId.bus.write8(withSubId.ram + 0xcf6a, 85);
            withSubId.bus.write8(withSubId.ram + 0xcf6b, 1);
            withSubId.bus.write8(withSubId.ram + 0x5042, 73);
            withSubId.cpu.regs().ep = 1; withSubId.cpu.regs().pc = 0x17c7;
            withSubId.runTo(0x17b1);
            if(withSubId.bus.read8(withSubId.ram + 0x5042) != 85)
                throw std::runtime_error("positive control did not change volume");
            std::cout << "Pro master volume: missing sub-ID leaves 73; sub-ID 01 changes it to 85\n";
        }
        std::cout << "PASS (ROM-handler fixture only)\n";
    }
    catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
