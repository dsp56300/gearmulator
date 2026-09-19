// Optional integration regression: requires user-supplied control ROMs.
#include "88lib/boards/sc88.h"
#include "88lib/boards/sc88pro.h"
#include "88lib/rom/rom.h"
#include "cpu/common/test_util.hpp"
#include "baseLib/os.h"
#include <fstream>
#include <iterator>
#include <iostream>

using namespace emu88Lib;
using namespace test;

template<class Board> struct Probe : Board
{
	using Board::Board;
	using Board::extRead8;
};

template<class Board> void exercise(Board& board, uint32_t assignments, uint32_t channels)
{
	CHECK(board.isValid());
	if(!board.isValid()) return;
	const auto run = [&] { for(unsigned i = 0; i < 32000; ++i) board.renderSample(); };
	run(); run(); run();
	const auto send = [&](uint8_t mid, uint8_t low, std::initializer_list<uint8_t> data, bool corrupt = false)
	{
		synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
		e.sysex = {0xf0, 0x41, 0x10, 0x42, 0x12, 0, mid, low};
		unsigned sum = mid + low;
		for(auto b : data) { e.sysex.push_back(b); sum += b; }
		e.sysex.push_back(static_cast<uint8_t>((-sum + corrupt) & 0x7f));
		e.sysex.push_back(0xf7);
		board.addMidiEvent(e, 0);
		run();
	};
	CHECK_EQ(board.extRead8(assignments + 16), 1);
	CHECK_EQ(board.extRead8(assignments + 17), 1);
	send(1, 16, {0});
	CHECK_EQ(board.extRead8(assignments + 16), 0);
	CHECK_EQ(board.extRead8(assignments + 17), 1);
	CHECK_EQ(board.extRead8(channels + 16) & 0x10, 0);
	CHECK_EQ(board.extRead8(channels + 17) & 0x10, 0x10);
	// A non-group-boundary address and a contiguous multi-part write.
	// Keep the first value unchanged: the Pro ROM's setter doubles R3
	// when a value changes, and its caller reuses R3 for the next part.
	// Do not hide that firmware quirk with host-side parameter writes.
	send(1, 17, {1, 0});
	CHECK_EQ(board.extRead8(assignments + 17), 1);
	CHECK_EQ(board.extRead8(assignments + 18), 0);
	CHECK_EQ(board.extRead8(assignments + 19), 1);
	send(1, 17, {0});
	CHECK_EQ(board.extRead8(assignments + 17), 0);
	send(1, 16, {1}, true);
	CHECK_EQ(board.extRead8(assignments + 16), 0);
	send(1, 16, {2}); // Firmware rejects COMPUTER for this parameter.
	CHECK_EQ(board.extRead8(assignments + 16), 0);
	send(1, 16, {1});
	CHECK_EQ(board.extRead8(assignments + 16), 1);
	// Mode transition is also firmware-owned, including receive-switch checks.
	send(0, 0x7f, {1});
	CHECK_EQ(board.extRead8((assignments & 0xff0000) + 0xfe2b) & 1, 1);
	send(0, 0x7f, {0});
	CHECK_EQ(board.extRead8((assignments & 0xff0000) + 0xfe2b) & 1, 0);
}

int main(int argc, char** argv)
{
	baseLib::disableErrorDialogs();
	if(argc != 4) return 77; // SC-88, SC-88VL, SC-88Pro control ROM paths.
	const auto load = [](const char* path)
	{
		std::ifstream in(path, std::ios::binary);
		std::vector<uint8_t> rom(std::istreambuf_iterator<char>(in), {});
		normalizeH8WordOrder(rom);
		return rom;
	};
	Probe<Sc88> sc88(load(argv[1]), {}, Model::Sc88);
	std::cerr << "SC-88\n";
	exercise(sc88, 0x088020, 0x08da20);
	Probe<Sc88> vl(load(argv[2]), {}, Model::Sc88VL);
	std::cerr << "SC-88VL\n";
	exercise(vl, 0x088020, 0x08da20);
	Probe<Sc88Pro> pro(load(argv[3]));
	std::cerr << "SC-88Pro\n";
	exercise(pro, 0xc05020, 0xc0ca20);
	return finish("sc88_system_dt1");
}
