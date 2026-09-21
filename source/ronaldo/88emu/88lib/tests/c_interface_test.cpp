// The C interface without any ROM: everything a host can get wrong has to come back as a
// return code, never as a crash, and a closed synth renders silence.
//
// With EMU88_C_INTERFACE_TEST_ROM_DIR pointing at a folder of dumps it also opens every device
// it finds complete there, plays a chord and expects to hear it.
#include "88lib/c_interface.h"
#include "common/test_util.hpp"

#include "baseLib/filesystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

namespace
{
	void testDevices()
	{
		CHECK(std::strlen(emu88_get_library_version_string()) > 0);

		const auto count = emu88_get_device_count();
		CHECK(count > 0);
		std::set<emu88_device_id> ids;
		for(int i = 0; i < count; ++i)
		{
			const auto id = emu88_get_device_id(i);
			CHECK(id >= 0);
			CHECK(ids.insert(id).second);
			CHECK(emu88_get_device_name(id) != nullptr);
			const auto ports = emu88_get_device_midi_port_count(id);
			CHECK(ports >= 1 && ports <= 4);
		}
		CHECK(ids.count(EMU88_DEVICE_SC88PRO) == 1);
		CHECK_EQ(emu88_get_device_midi_port_count(EMU88_DEVICE_SC8850), 4);

		CHECK_EQ(emu88_get_device_id(-1), -1);
		CHECK_EQ(emu88_get_device_id(count), -1);
		CHECK(emu88_get_device_name(-1) == nullptr);
		CHECK(emu88_get_device_name(1000) == nullptr);
		CHECK_EQ(emu88_get_device_midi_port_count(1000), 0);
		CHECK_EQ(emu88_is_device_available(1000), 0);
	}

	void testRomPaths(const std::string& _emptyDir)
	{
		CHECK_EQ(emu88_set_rom_path(nullptr), EMU88_RC_INVALID_ARGUMENT);
		CHECK_EQ(emu88_set_rom_path(""), EMU88_RC_INVALID_ARGUMENT);
		CHECK_EQ(emu88_add_rom_path("88emu_no_such_folder"), EMU88_RC_PATH_NOT_FOUND);
		CHECK_EQ(emu88_set_rom_path(_emptyDir.c_str()), EMU88_RC_OK);

		for(int i = 0; i < emu88_get_device_count(); ++i)
			CHECK_EQ(emu88_is_device_available(emu88_get_device_id(i)), 0);

		// snprintf semantics: the whole length comes back, the copy is cut and terminated.
		const auto length = emu88_describe_device_roms(EMU88_DEVICE_SC88PRO, nullptr, 0);
		CHECK(length > 0);
		std::array<char, 8> small{};
		small.fill('x');
		CHECK_EQ(emu88_describe_device_roms(EMU88_DEVICE_SC88PRO, small.data(), small.size()), length);
		CHECK_EQ(std::strlen(small.data()), small.size() - 1);
		CHECK_EQ(emu88_describe_device_roms(1000, small.data(), small.size()), size_t(0));
		CHECK_EQ(small[0], char(0));
	}

	void testClosedContext()
	{
		// NULL everywhere.
		emu88_free_context(nullptr);
		emu88_close_synth(nullptr);
		CHECK_EQ(emu88_is_open(nullptr), 0);
		CHECK_EQ(emu88_open_synth(nullptr), EMU88_RC_INVALID_ARGUMENT);
		CHECK_EQ(emu88_play_msg(nullptr, 0x7f4090), EMU88_RC_INVALID_ARGUMENT);

		const auto context = emu88_create_context();
		CHECK(context != nullptr);
		CHECK_EQ(emu88_is_open(context), 0);
		CHECK_EQ(emu88_get_actual_stereo_output_samplerate(context), uint32_t(0));
		CHECK_EQ(emu88_get_midi_port_count(context), 0);

		CHECK_EQ(emu88_select_device(context, 1000), EMU88_RC_UNKNOWN_DEVICE);
		CHECK_EQ(emu88_select_device(context, EMU88_DEVICE_SC88PRO), EMU88_RC_OK);
		// The search path is the empty folder of testRomPaths().
		CHECK_EQ(emu88_open_synth(context), EMU88_RC_MISSING_ROMS);
		CHECK_EQ(emu88_is_open(context), 0);

		CHECK_EQ(emu88_play_msg(context, 0x7f4090), EMU88_RC_NOT_OPENED);
		const uint8_t gmReset[] = {0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7};
		CHECK_EQ(emu88_play_sysex(context, gmReset, sizeof(gmReset)), EMU88_RC_NOT_OPENED);
		CHECK_EQ(emu88_play_sysex(context, gmReset, sizeof(gmReset) - 1), EMU88_RC_INVALID_ARGUMENT);
		CHECK_EQ(emu88_play_msg(context, 0x000040), EMU88_RC_INVALID_ARGUMENT);	// no status byte
		CHECK_EQ(emu88_play_msg(context, 0x0000f0), EMU88_RC_INVALID_ARGUMENT);	// SysEx is not a short message

		std::array<float, 64> floats;
		floats.fill(1.0f);
		emu88_render_float(context, floats.data(), floats.size() / 2);
		CHECK(std::all_of(floats.begin(), floats.end(), [](const float _v) { return _v == 0.0f; }));
		std::array<int16_t, 64> ints;
		ints.fill(1);
		emu88_render_bit16s(context, ints.data(), ints.size() / 2);
		CHECK(std::all_of(ints.begin(), ints.end(), [](const int16_t _v) { return _v == 0; }));
		emu88_render_float(context, nullptr, 16);
		emu88_render_float(nullptr, floats.data(), 0);

		emu88_free_context(context);
	}

	// Needs ROMs: every complete device opens, takes a chord on each of its inputs and sounds.
	void testWithRoms(const char* _romDir)
	{
		CHECK_EQ(emu88_set_rom_path(_romDir), EMU88_RC_OK);
		for(int i = 0; i < emu88_get_device_count(); ++i)
		{
			const auto id = emu88_get_device_id(i);
			if(!emu88_is_device_available(id))
				continue;
			std::printf("c_interface: opening %s\n", emu88_get_device_name(id));

			const auto context = emu88_create_context();
			CHECK_EQ(emu88_select_device(context, id), EMU88_RC_OK);
			emu88_set_stereo_output_samplerate(context, 48000.0);
			CHECK_EQ(emu88_open_synth(context), EMU88_RC_OK);
			CHECK_EQ(emu88_is_open(context), 1);
			CHECK_EQ(emu88_get_actual_stereo_output_samplerate(context), uint32_t(48000));
			CHECK(emu88_get_device_samplerate(context) > 0);
			CHECK_EQ(emu88_get_midi_port_count(context), emu88_get_device_midi_port_count(id));

			for(int port = 0; port < emu88_get_midi_port_count(context); ++port)
				for(const uint32_t key : {60u, 64u, 67u})
					CHECK_EQ(emu88_play_msg_on_port(context, static_cast<unsigned>(port), 0x90u | key << 8 | 100u << 16), EMU88_RC_OK);

			std::array<float, 2048> block;
			float peak = 0.0f;
			for(int blocks = 0; blocks < 48; ++blocks)	// ~1 s
			{
				emu88_render_float(context, block.data(), block.size() / 2);
				for(const auto sample : block)
					peak = std::max(peak, std::fabs(sample));
			}
			CHECK(peak > 0.01f);
			CHECK(peak <= 1.0f);

			emu88_close_synth(context);
			CHECK_EQ(emu88_is_open(context), 0);
			emu88_free_context(context);
		}
	}
}

int main()
{
	const auto emptyDir = baseLib::filesystem::validatePath("88emu_c_interface_test");
	baseLib::filesystem::createDirectory(emptyDir);

	testDevices();
	testRomPaths(emptyDir);
	testClosedContext();
	if(const auto* romDir = std::getenv("EMU88_C_INTERFACE_TEST_ROM_DIR"))
		testWithRoms(romDir);

	return test::finish("88lib c_interface");
}
