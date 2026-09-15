#include "RcpFilePlayer.h"
#include "cpu/common/test_util.hpp"
#include "midifile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>

namespace
{
	using Command = std::array<uint8_t, 4>;
	struct Track
	{
		uint8_t channel = 0;
		uint8_t transpose = 0;
		bool muted = false;
		std::vector<Command> commands{{60, 48, 24, 100}, {0xfe, 0, 0, 0}};
	};

	std::vector<uint8_t> makeFile(const std::vector<Track>& tracks)
	{
		std::vector<uint8_t> data(0x586);
		const std::string header = "RCM-PC98V2.0(C)COME ON MUSIC";
		std::copy(header.begin(), header.end(), data.begin());
		data[0x1c0] = 48;
		data[0x1c1] = 120;
		data[0x1c2] = data[0x1c3] = 4;
		data[0x1e6] = static_cast<uint8_t>(tracks.size());
		for (const auto& track : tracks)
		{
			const auto start = data.size();
			const auto length = 0x2c + track.commands.size() * 4;
			data.resize(start + length);
			data[start] = static_cast<uint8_t>(length);
			data[start + 1] = static_cast<uint8_t>(length >> 8);
			data[start + 4] = track.channel;
			data[start + 5] = track.transpose;
			data[start + 7] = track.muted;
			auto offset = start + 0x2c;
			for (const auto& command : track.commands)
				for (const auto byte : command)
					data[offset++] = byte;
		}
		return data;
	}
} // namespace

int main(int argc, char** argv)
{
	std::vector<sc88smf::Event> events;
	std::string error;
	auto data = makeFile({Track{}});
	CHECK(rcpfile::loadRcpV2(data, events, error));
	CHECK_EQ(events.size(), size_t{2});
	CHECK(events[0].bytes == std::vector<uint8_t>({0x90, 60, 100}));
	CHECK(events[1].bytes == std::vector<uint8_t>({0x80, 60, 0}));
	CHECK(std::abs(events[1].seconds - 0.25) < 1.0e-9);

	std::vector<Track> tracks(36);
	for (auto& track : tracks)
		track.muted = true;
	tracks[0].muted = tracks[35].muted = false;
	tracks[0].channel = 31;
	tracks[35].channel = 2;
	data = makeFile(tracks);
	CHECK(rcpfile::loadRcpV2(data, events, error));
	CHECK_EQ(events.size(), size_t{4});
	CHECK_EQ(events[0].port, 1);
	CHECK_EQ(events[0].bytes[0], 0x9f);
	CHECK_EQ(events[1].port, 0);
	CHECK_EQ(events[1].bytes[0], 0x92);

	Track tempo;
	tempo.transpose = 2;
	tempo.commands = {{0xe7, 0, 32, 0}, {60, 48, 48, 100}, {0xeb, 0, 64, 127}, {0xfe, 0, 0, 0}};
	CHECK(rcpfile::loadRcpV2(makeFile({tempo}), events, error));
	CHECK_EQ(events.front().bytes[1], 62);
	CHECK(std::abs(events.back().seconds - 1.0) < 1.0e-9);
	CHECK(events.back().bytes == std::vector<uint8_t>({0xb0, 64, 127}));

	Track tempoDuringNote;
	tempoDuringNote.commands = {{60, 24, 48, 100}, {0xe7, 0, 32, 0}, {0xfe, 0, 0, 0}};
	CHECK(rcpfile::loadRcpV2(makeFile({tempoDuringNote}), events, error));
	CHECK_EQ(events.size(), size_t{2});
	if (events.size() == 2)
	{
		CHECK(events[1].bytes == std::vector<uint8_t>({0x80, 60, 0}));
		CHECK(std::abs(events[1].seconds - 0.75) < 1.0e-9);
	}

	Track loop;
	loop.commands = {{0xf9, 0, 0, 0}, {60, 48, 24, 100}, {0xf8, 3, 0, 0}, {0xfe, 0, 0, 0}};
	CHECK(rcpfile::loadRcpV2(makeFile({loop}), events, error));
	CHECK_EQ(events.size(), size_t{6});
	CHECK(std::abs(events.back().seconds - 1.25) < 1.0e-9);

	Track sysex;
	sysex.channel = 16;
	sysex.commands = {{0x98, 0, 0x7e, 0x7f}, {0xf7, 0, 9, 1}, {0xf7, 0, 0xf7, 0}, {0xfe, 0, 0, 0}};
	CHECK(rcpfile::loadRcpV2(makeFile({sysex}), events, error));
	CHECK_EQ(events.size(), size_t{1});
	CHECK(events[0].bytes == std::vector<uint8_t>({0xf0, 0x7e, 0x7f, 9, 1, 0xf7}));
	CHECK_EQ(events[0].port, 1);

	data.resize(20);
	CHECK(!rcpfile::loadRcpV2(data, events, error));
	CHECK(events.empty());
	CHECK(!error.empty());

	if (argc > 1)
	{
		std::ifstream input(argv[1], std::ios::binary);
		std::vector<uint8_t> song((std::istreambuf_iterator<char>(input)), {});
		CHECK(rcpfile::loadRcpV2(song, events, error));
		CHECK(events.size() > 100);
		CHECK(std::is_sorted(events.begin(), events.end(),
							 [](const auto& a, const auto& b) { return a.seconds < b.seconds; }));
		std::printf("File events: %zu\n", events.size());
	}
	return test::finish("RcpFilePlayer");
}
