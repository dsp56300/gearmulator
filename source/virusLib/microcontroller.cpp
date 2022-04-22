#include <vector>
#include <chrono>
#include <thread>
#include <cstring> // memcpy

#include "microcontroller.h"

#include "../synthLib/midiTypes.h"

using namespace dsp56k;
using namespace synthLib;

constexpr virusLib::PlayMode g_defaultPlayMode = virusLib::PlayModeSingle;

constexpr uint32_t g_sysexPresetHeaderSize = 9;
constexpr uint32_t g_sysexPresetFooterSize = 2;	// checksum, f7

constexpr uint32_t g_singleRamBankCount = 2;

constexpr uint8_t g_pageA[] = {0x05, 0x0A, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D,
							   0x1E, 0x1F, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D,
							   0x2E, 0x2F, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D,
							   0x3E, 0x3F, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
							   0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5D, 0x5E, 0x61,
							   0x62, 0x63, 0x64, 0x65, 0x66, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x7B};
constexpr uint8_t g_pageB[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x11,
							   0x12, 0x13, 0x15, 0x19, 0x1A, 0x1B, 0x1C, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24,
							   0x26, 0x27, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x36, 0x37,
							   0x38, 0x39, 0x3A, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
							   0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x54, 0x55,
							   0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63,
							   0x64, 0x65, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x7B, 0x7C};

constexpr uint8_t
	g_pageC_global[] = {45,  63,  64,  65,  66,  67,  68,  69,  70,  85,  86,  87,  90,  91,
						92,  93,  94,  95,  96,  97,  98,  99,  105, 106, 110, 111, 112, 113,
						114, 115, 116, 117, 118, 120, 121, 122, 123, 124, 125, 126, 127};
constexpr uint8_t g_pageC_multi[]      = {5,6,7,8,9,10,11,12,13,14,22,31,32,33,34,35,36,37,38,39,40,41,72,73,74,75,77,78};
constexpr uint8_t g_pageC_multiPart[]  = {31,32,33,34,35,36,37,38,39,40,41,72,73,74,75,77,78};

namespace virusLib
{

static uint8_t calcChecksum(const std::vector<uint8_t>& _data, const size_t _offset)
{
	uint8_t cs = 0;

	for (size_t i = _offset; i < _data.size(); ++i)
		cs += _data[i];

	return cs & 0x7f;
}

constexpr int g_presetWriteDelaySamples = 8;

Microcontroller::Microcontroller(HDI08& _hdi08, const ROMFile& _romFile) : m_hdi08(_hdi08), m_rom(_romFile), m_pendingPresetWriteDelay(g_presetWriteDelaySamples)
{
	if(!_romFile.isValid())
		return;

	m_globalSettings.fill(0);

	m_rom.getMulti(0, m_multiEditBuffer);

	bool failed = false;

	// read all singles from ROM and copy first ROM banks to RAM banks
	for(uint32_t b=0; b<8 && !failed; ++b)
	{
		std::vector<TPreset> singles;

		const auto bank = b >= g_singleRamBankCount ? b - g_singleRamBankCount : b;

		for(uint32_t p=0; p<m_rom.getPresetsPerBank(); ++p)
		{
			TPreset single;
			m_rom.getSingle(bank, p, single);

			if(ROMFile::getSingleName(single).size() != 10)
			{
				failed = true;
				break;				
			}

			singles.emplace_back(single);
		}

		if(!singles.empty())
			m_singles.emplace_back(std::move(singles));
	}

	if(!m_singles.empty())
	{
		const auto& singles = m_singles[0];

		if(!singles.empty())
		{
			m_singleEditBuffer = singles[0];

			for(auto i=0; i<static_cast<int>(std::min(singles.size(), m_singleEditBuffers.size())); ++i)
				m_singleEditBuffers[i] = singles[i];
		}
	}
}

void Microcontroller::sendInitControlCommands()
{
	writeHostBitsWithWait(0, 1);
//	const std::vector<TWord> magic = { 0xf4f473, 0xf4f46e, 0xf4f46f, 0xf4f477 };	// snow
//	const std::vector<TWord> magic = { 0xf4f453, 0xf4f44e, 0xf4f44f, 0xf4f457 };	// SNOW
//	const std::vector<TWord> magic = { 0xf4f454, 0xf4f449, 0xf4f453, 0xf4f44e, 0xf4f44f, 0xf4f457 };	// TISNOW
//	const std::vector<TWord> magic = { 0xf4f473, 0x407f01, 0xf4f473, 0x401000, 0xf4f46e, 0x407f01, 0xf4f46e, 0x401000, 0xf4f46f, 0x407f01, 0xf4f46f, 0x401000, 0xf4f477, 0x407f01, 0xf4f477, 0x401000 };
//	const std::vector<TWord> magic = { 0xf4f473, 0x407f00, 0xf4f46e, 0x407f01, 0xf4f46f, 0x407f02, 0xf4f477, 0x407f03 };
//	m_hdi08.writeRX(magic);

	LOG("Sending Init Control Commands");

	if(m_rom.isTIFamily())
	{
		const std::vector<TWord> initCodeDS =
		{
			0xF4F473, 0x407F00,
			0xF4F473, 0x401000,						// Samplerate 44100 Hz
//			0xF47555, 0x104000, 0x0C0104, 0x000319, 0x007F00, 0x00407F, 0x000000, 0x00007E, 0x003728, 0x607F62, 0x3E3420, 0x190040, 0x406000, 0x663100, 0x402300, 0x401509, 0x2F233E, 0x286B6A, 0x400600, 0x010200, 0x384719, 0x137F00, 0x7F7F40, 0x150000, 0x006501, 0x010057, 0x000040, 0x404040, 0x4B5402, 0x010040, 0x000040, 0x404040, 0x407B01, 0x400400, 0x000269, 0x7F4000, 0x01017F, 0x001060, 0x126801, 0x00011B, 0x7F5010, 0x0C0140, 0x000000, 0x010000, 0x000040, 0x000000, 0x004000, 0x610100, 0x000100, 0x4B0000, 0x390400, 0x000000, 0x7F0000, 0x01423E, 0x010001, 0x000124, 0x000040, 0x000000, 0x000040, 0x40282B, 0x554040, 0x404049, 0x2C4060, 0x4D4040, 0x004040, 0x401603, 0x03107F, 0x144900, 0x004002, 0x490F19, 0x2B186A, 0x184814, 0x010000, 0x410000, 0x7F607F, 0x004864, 0x334040, 0x282000, 0x000000, 0x056556, 0x071845, 0x023C00, 0x202054, 0x202049, 0x202042, 0x430001, 0x000100, 0x010001, 0x440354, 0x373062, 0x000000, 0x400304, 0x020000, 0x000000, 0x7F4040, 0x7F7F40, 0x000005, 0x000200, 0x000000, 0x010000, 0x000000, 0x004000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x400000, 0x000000, 0x000000, 0x007F7F, 0x400000, 0x000000, 0x144600, 0x404614, 0x460040, 0x460135, 0x004000, 0x400040, 0x004000, 0x400040, 0x0B2903, 0x400000, 0x000000, 0x000000, 0x010002, 0x000000, 0x010000, 0x00001F, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x02697F, 0x400001, 0x000006,
			0xF4F473, 0x401000,						// Samplerate 44100 Hz
			0xF00020, 0x330110, 0x734000, 0x00F700,
			0xF00020, 0x330110, 0x734009, 0x02F702,	// USB Mode = 3 out 1 in
			0xF00020, 0x330110, 0x73400E, 0x00F700,
			0xF00020, 0x330110, 0x73400D, 0x00F700,
			0xF00020, 0x330110, 0x734010, 0x00F700,
			0xF00020, 0x330110, 0x734019, 0x01F701,
			0xF00020, 0x330110, 0x73401A, 0x01F701,
			0xF00020, 0x330110, 0x73401B, 0x01F701,
			0xF00020, 0x330110, 0x73401C, 0x01F701,
			0xF00020, 0x330110, 0x73401D, 0x00F700,
			0xF00020, 0x330110, 0x73402D, 0x00F700,	// Second Output Select = 0
			0xF00020, 0x330110, 0x734032, 0x6EF76E,
			0xF00020, 0x330110, 0x734033, 0x50F750,
			0xF00020, 0x330110, 0x734034, 0x20F720,
			0xF00020, 0x330110, 0x734035, 0x40F740,
			0xF00020, 0x330110, 0x734036, 0x0CF70C,
			0xF00020, 0x330110, 0x73403B, 0x01F701,
			0xF00020, 0x330110, 0x73403C, 0x01F701,
			0xF00020, 0x330110, 0x73403E, 0x40F740,
			0xF00020, 0x330110, 0x734040, 0x01F701,
			0xF00020, 0x330110, 0x734041, 0x00F700,
			0xF00020, 0x330110, 0x734042, 0x40F740,
			0xF00020, 0x330110, 0x734043, 0x01F701,
			0xF00020, 0x330110, 0x734044, 0x40F740,
			0xF00020, 0x330110, 0x734045, 0x00F700,
			0xF00020, 0x330110, 0x734046, 0x40F740,
			0xF00020, 0x330110, 0x73404C, 0x00F700,
			0xF00020, 0x330110, 0x734057, 0x01F701,
			0xF00020, 0x330110, 0x73405A, 0x00F700,	// Input Thru Level = 0
			0xF00020, 0x330110, 0x73405B, 0x00F700, // Input Boost = 0
			0xF00020, 0x330110, 0x73405C, 0x40F740,	// Master Tune = +/- 0
			0xF00020, 0x330110, 0x73405D, 0x10F710, // device ID $10 = omni
			0xF00020, 0x330110, 0x73405E, 0x01F701, // Midi Control Low Page = 1 = allow midi CC
			0xF00020, 0x330110, 0x73405F, 0x00F700, // Midi Control High Page = 0 = Do NOT allow Poly Pressure
			0xF00020, 0x330110, 0x734060, 0x00F700, // Midi Arp Send = 0 = off
			0xF00020, 0x330110, 0x73406A, 0x01F701, // Midi Clock RX = 1 = enabled
			0xF00020, 0x330110, 0x73406D, 0x64F764,
			0xF00020, 0x330110, 0x73406E, 0x00F700,
			0xF00020, 0x330110, 0x73406F, 0x00F700,
			0xF00020, 0x330110, 0x734070, 0x00F700,
			0xF00020, 0x330110, 0x734071, 0x00F700,
			0xF00020, 0x330110, 0x734072, 0x00F700,
//			0xF0FFFF, 0x00FFFF, 0x20FFFF, 0x33FFFF, 0x01FFFF, 0x10FFFF, 0x73FFFF, 0x01FFFF, 0x10FFFF, 0x00FFFF, 0xF7FFFF,	// parameter $10 for Part 1 = 0
			0xF00020, 0x330110, 0x734073, 0x00F700,
			0xF00020, 0x330110, 0x734079, 0x01F701,
			0xF00020, 0x330110, 0x73407C, 0x00F700,	// Global Channel = 0
			0xF00020, 0x330110, 0x73407D, 0x02F702, // LED Mode = 2
			0xF00020, 0x330110, 0x73407F, 0x63F763,	// Master Volume = 99
//			0xF47555, 0x104000, 0x0C0104, 0x000319, 0x007F00, 0x00407F, 0x000000, 0x00007E, 0x003728, 0x607F62, 0x3E3420, 0x190040, 0x406000, 0x663100, 0x402300, 0x401509, 0x2F233E, 0x286B6A, 0x400600, 0x010200, 0x384719, 0x137F00, 0x7F7F40, 0x150000, 0x006501, 0x010057, 0x000040, 0x404040, 0x4B5402, 0x010040, 0x000040, 0x404040, 0x407B01, 0x400400, 0x000269, 0x7F4000, 0x01017F, 0x001060, 0x126801, 0x00011B, 0x7F5010, 0x0C0140, 0x000000, 0x010000, 0x000040, 0x000000, 0x004000, 0x610100, 0x000100, 0x4B0000, 0x390400, 0x000000, 0x7F0000, 0x01423E, 0x010001, 0x000124, 0x000040, 0x000000, 0x000040, 0x40282B, 0x554040, 0x404049, 0x2C4060, 0x4D4040, 0x004040, 0x401603, 0x03107F, 0x144900, 0x004002, 0x490F19, 0x2B186A, 0x184814, 0x010000, 0x410000, 0x7F607F, 0x004864, 0x334040, 0x282000, 0x000000, 0x056556, 0x071845, 0x023C00, 0x202054, 0x202049, 0x202042, 0x430001, 0x000100, 0x010001, 0x440354, 0x373062, 0x000000, 0x400304, 0x020000, 0x000000, 0x7F4040, 0x7F7F40, 0x000005, 0x000200, 0x000000, 0x010000, 0x000000, 0x004000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x400000, 0x000000, 0x000000, 0x007F7F, 0x400000, 0x000000, 0x144600, 0x404614, 0x460040, 0x460135, 0x004000, 0x400040, 0x004000, 0x400040, 0x0B2903, 0x400000, 0x000000, 0x000000, 0x010002, 0x000000, 0x010000, 0x00001F, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x406401, 0x406400, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x02697F, 0x400001, 0x000006,
			0xF4F473, 0x401000,						// Samplerate 44100 Hz
//			0xF4F473, 0x401001,						// Samplerate 48000 Hz
		};

		m_hdi08.writeRX(initCodeDS);

#if 1
		constexpr uint8_t prts = 0x4f;

		enum class Output : uint8_t
		{
			Out1L,		Out1,		Out1R,
			Out2L,		Out2,		Out2R,
			Out3L,		Out3,		Out3R,
			Usb1L,		Usb1,		Usb1R,
			Usb2L,		Usb2,		Usb2R,
			Usb3L,		Usb3,		Usb3R
		};

		constexpr auto oa = static_cast<uint8_t>(Output::Usb1);
		constexpr auto ob = static_cast<uint8_t>(Output::Usb2);
		constexpr auto oc = static_cast<uint8_t>(Output::Usb3);

		constexpr uint8_t multi[] =
		{
			0x02,0x01,0x00,0x01,0x49,0x6e,0x69,0x74,0x20,0x4d,0x75,0x6c,0x74,0x69,0x00,0x39,	// Internal/"Init Multi"/Clock Tempo
			0x01,0x3c,0x00,0x10,0x00,0x01,0x01,0x00,0x40,0x40,0x40,0x40,0x40,0x00,0x40,0x40,	// Delay/Internal
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,	// Bank Number
			0x00,0x00,0x00,0x00,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,	// Program Number
			0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,	// Midi Channel
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,	// Low Key
			0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,	// High Key
			0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,	// Transpose
			0x40,0x42,0x43,0x41,0x47,0x42,0x46,0x41,0x48,0x46,0x44,0x40,0x40,0x40,0x40,0x40,	// Detune
			0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,	// Part Volume
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,	// Midi Volume Init
			oa  ,oa  ,ob  ,ob  ,oc  ,oc  ,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,	// Output Select
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,	// Effect Send
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,	// Internal
			0x41,0x46,0x40,0x48,0x41,0x49,0x47,0x41,0x42,0x47,0x40,0x45,0x41,0x49,0x47,0x46,	// Internal
			prts,prts,prts,prts,prts,prts,prts,prts,prts,prts,prts,prts,prts,prts,prts,prts		// Part State
		};

		TPreset data;
		memcpy(&data[0], multi, std::size(multi));

		sendControlCommand(PLAY_MODE, PlayModeMulti);

		const auto words = presetToDSPWords(data, true);

		sendPreset(0, words, true);
#endif
	}
	else
	{
		sendControlCommand(MIDI_CLOCK_RX, 0x1);				// Enable MIDI clock receive
		sendControlCommand(GLOBAL_CHANNEL, 0x0);			// Set global midi channel to 0
		sendControlCommand(MIDI_CONTROL_LOW_PAGE, 0x1);		// Enable midi CC to edit parameters on page A
		sendControlCommand(MIDI_CONTROL_HIGH_PAGE, 0x1);	// Enable poly pressure to edit parameters on page B
		sendControlCommand(MASTER_VOLUME, 127);				// Set master volume to maximum
		sendControlCommand(MASTER_TUNE, 64);				// Set master tune to 0
	}
}

void Microcontroller::createDefaultState()
{
	sendControlCommand(PLAY_MODE, g_defaultPlayMode);

	if constexpr (g_defaultPlayMode == PlayModeSingle)
		writeSingle(BankNumber::EditBuffer, SINGLE, m_singleEditBuffer);
	else
		loadMulti(0, m_multiEditBuffer);
}

bool Microcontroller::needsToWaitForHostBits(const uint8_t _flag0, const uint8_t _flag1) const
{
	return m_hdi08.needsToWaitForHostFlags(_flag0, _flag1);
}

void Microcontroller::writeHostBitsWithWait(const uint8_t flag0, const uint8_t flag1) const
{
	std::lock_guard lock(m_mutex);
	m_hdi08.setHostFlagsWithWait(flag0, flag1);
}

bool Microcontroller::sendPreset(const uint8_t program, const std::vector<TWord>& preset, const bool isMulti)
{
	std::lock_guard lock(m_mutex);

	if(m_loadingState || m_hdi08.hasDataToSend() || needsToWaitForHostBits(0,1))
	{
		// if we write a multi or a multi mode single, remove a pending single for single mode
		// If we write a single-mode single, remove all multi-related pending writes
		const auto multiRelated = isMulti || program != SINGLE;

		for (auto it = m_pendingPresetWrites.begin(); it != m_pendingPresetWrites.end();)
		{
			const auto& pendingPreset = *it;

			const auto pendingIsMultiRelated = pendingPreset.isMulti || pendingPreset.program != SINGLE;

			if (multiRelated != pendingIsMultiRelated)
				it = m_pendingPresetWrites.erase(it);
			else
				++it;
		}

		for(auto it = m_pendingPresetWrites.begin(); it != m_pendingPresetWrites.end();)
		{
			const auto& pendingPreset = *it;
			if (pendingPreset.isMulti == isMulti && pendingPreset.program == program)
				it = m_pendingPresetWrites.erase(it);
			else
				++it;
		}

		m_pendingPresetWrites.emplace_back(SPendingPresetWrite{program, isMulti, preset});

		return true;
	}

	writeHostBitsWithWait(0,1);
	// Send header
	TWord buf[] = {0xf47555, static_cast<TWord>(isMulti ? 0x110000 : 0x100000)};
	buf[1] = buf[1] | (program << 8);
	m_hdi08.writeRX(buf, 2);

	m_hdi08.writeRX(preset);

	m_pendingPresetWriteDelay = g_presetWriteDelaySamples;

	return true;
}

void Microcontroller::sendControlCommand(const ControlCommand _command, const uint8_t _value)
{
	send(m_rom.isTIFamily() ? PAGE_D : PAGE_C, 0x0, _command, _value);
}

bool Microcontroller::send(const Page _page, const uint8_t _part, const uint8_t _param, const uint8_t _value)
{
	std::lock_guard lock(m_mutex);

	writeHostBitsWithWait(0,1);

	TWord buf[] = {0xf4f400, 0x0};
	buf[0] = buf[0] | _page;
	buf[1] = (_part << 16) | (_param << 8) | _value;
	m_hdi08.writeRX(buf, 2);

	if(_page == (m_rom.isTIFamily() ? PAGE_D : PAGE_C))
	{
		m_globalSettings[_param] = _value;
	}
	return true;
}

bool Microcontroller::sendMIDI(const SMidiEvent& _ev, bool cancelIfFull/* = false*/)
{
	const uint8_t channel = _ev.a & 0x0f;
	const uint8_t status = _ev.a & 0xf0;

	const auto singleMode = m_globalSettings[PLAY_MODE] == PlayModeSingle;

	if(status != 0xf0 && singleMode && channel != m_globalSettings[GLOBAL_CHANNEL])
		return true;

	switch (status)
	{
	case M_PROGRAMCHANGE:
		{
			if(singleMode)
				return partProgramChange(SINGLE, _ev.b);
			return partProgramChange(channel, _ev.b);
		}
	case M_CONTROLCHANGE:
		switch(_ev.b)
		{
		case MC_BANKSELECTLSB:
			if(singleMode)
				partBankSelect(SINGLE, _ev.c, false);
			else
				partBankSelect(channel, _ev.c, false);
			return true;
		default:
			applyToSingleEditBuffer(PAGE_A, singleMode ? SINGLE : channel, _ev.b, _ev.c);
			break;
		}
		break;
	case M_POLYPRESSURE:
		applyToSingleEditBuffer(PAGE_B, singleMode ? SINGLE : channel, _ev.b, _ev.c);
		break;
	default:
		break;
	}

	m_pendingMidiEvents.push_back(_ev);
	return true;
}

bool Microcontroller::sendSysex(const std::vector<uint8_t>& _data, std::vector<SMidiEvent>& _responses, const MidiEventSource _source)
{
	if (_data.size() < 7)
		return true;	// invalid sysex or not directed to us

	const auto manufacturerA = _data[1];
	const auto manufacturerB = _data[2];
	const auto manufacturerC = _data[3];
	const auto productId = _data[4];
	const auto deviceId = _data[5];
	const auto cmd = _data[6];

	if (deviceId != m_globalSettings[DEVICE_ID] && deviceId != OMNI_DEVICE_ID && m_globalSettings[DEVICE_ID] != OMNI_DEVICE_ID)
	{
		// ignore messages intended for a different device, allow omni requests
		return true;
	}

	auto buildResponseHeader = [&](SMidiEvent& ev)
	{
		auto& response = ev.sysex;

		response.reserve(1024);

		response.push_back(M_STARTOFSYSEX);
		response.push_back(manufacturerA);
		response.push_back(manufacturerB);
		response.push_back(manufacturerC);
		response.push_back(productId);
		response.push_back(deviceId);
	};

	auto buildPresetResponse = [&](const uint8_t _type, const BankNumber _bank, const uint8_t _program, const TPreset& _dump)
	{
		SMidiEvent ev;
		ev.source = _source;

		auto& response = ev.sysex;

		buildResponseHeader(ev);

		response.push_back(_type);
		response.push_back(toMidiByte(_bank));
		response.push_back(_program);

		const auto size = _type == DUMP_SINGLE ? m_rom.getSinglePresetSize() : m_rom.getMultiPresetSize();

		const auto modelABCsize = ROMFile::getSinglePresetSize(ROMFile::Model::ABC);

		for(size_t i=0; i<modelABCsize; ++i)
			response.push_back(_dump[i]);

		// checksum for ABC models comes after 256 bytes of preset data
		response.push_back(calcChecksum(response, 5));

		// editor cannot handle D presets yet
		if(_source != MidiEventSourceEditor)
		{
			if (size > modelABCsize)
			{
				for (size_t i = modelABCsize; i < size; ++i)
					response.push_back(_dump[i]);

				// Second checksum for D model: That checksum is to be calculated over the whole preset data, including the ABC checksum
				response.push_back(calcChecksum(response, 5));
			}
		}

		response.push_back(M_ENDOFSYSEX);

		_responses.emplace_back(std::move(ev));
	};

	auto buildSingleResponse = [&](const BankNumber _bank, const uint8_t _program)
	{
		TPreset dump;
		const auto res = requestSingle(_bank, _program, dump);
		if(res)
			buildPresetResponse(DUMP_SINGLE, _bank, _program, dump);
	};

	auto buildMultiResponse = [&](const BankNumber _bank, const uint8_t _program)
	{
		TPreset dump;
		const auto res = requestMulti(_bank, _program, dump);
		if(res)
			buildPresetResponse(DUMP_MULTI, _bank, _program, dump);
	};

	auto buildSingleBankResponse = [&](const BankNumber _bank)
	{
		if (_bank == BankNumber::EditBuffer)
			return;

		const auto bankIndex = toArrayIndex(_bank);

		if(bankIndex < m_singles.size())
		{
			// eat this, host, whoever you are. 128 single packets
			for(uint8_t i=0; i<m_singles[bankIndex].size(); ++i)
			{
				TPreset data;
				const auto res = requestSingle(_bank, i, data);
				buildPresetResponse(DUMP_SINGLE, _bank, i, data);
			}
		}		
	};

	auto buildMultiBankResponse = [&](const BankNumber _bank)
	{
		if(_bank == BankNumber::A)
		{
			// eat this, host, whoever you are. 128 multi packets
			for(uint8_t i=0; i<m_rom.getPresetsPerBank(); ++i)
			{
				TPreset data;
				const auto res = requestMulti(_bank, i, data);
				buildPresetResponse(DUMP_MULTI, _bank, i, data);
			}
		}
	};

	auto buildGlobalResponse = [&](const uint8_t _param)
	{
		SMidiEvent ev;
		ev.source = _source;
		auto& response = ev.sysex;

		buildResponseHeader(ev);

		response.push_back(PARAM_CHANGE_C);
		response.push_back(0);	// part = 0
		response.push_back(_param);
		response.push_back(m_globalSettings[_param]);
		response.push_back(M_ENDOFSYSEX);

		_responses.emplace_back(std::move(ev));
	};

	auto buildGlobalResponses = [&]()
	{
		for (const auto globalParam : g_pageC_global)
			buildGlobalResponse(globalParam);
	};

	auto buildTotalResponse = [&]()
	{
		buildGlobalResponses();
		buildSingleBankResponse(BankNumber::A);
		buildSingleBankResponse(BankNumber::B);
		buildMultiBankResponse(BankNumber::A);
	};

	auto buildArrangementResponse = [&]()
	{
		// If we are in multi mode, we return the Single mode single first. If in single mode, it is returned last.
		// The reason is that we want to backup everything but the last loaded multi/single defines the play mode when restoring
		const bool isMultiMode = m_globalSettings[PLAY_MODE] == PlayModeMulti;

		if(isMultiMode)
			buildSingleResponse(BankNumber::EditBuffer, SINGLE);

		buildMultiResponse(BankNumber::EditBuffer, 0);

		for(uint8_t p=0; p<16; ++p)
			buildPresetResponse(DUMP_SINGLE, BankNumber::EditBuffer, p, m_singleEditBuffers[p]);

		if(!isMultiMode)
			buildSingleResponse(BankNumber::EditBuffer, SINGLE);
	};

	auto buildControllerDumpResponse = [&](uint8_t _part)
	{
		TPreset _dump, _multi;
		const auto res = requestSingle(BankNumber::EditBuffer, _part, _dump);
		const auto resm = requestMulti(BankNumber::EditBuffer, 0, _multi);
		const uint8_t channel = _part == SINGLE ? m_globalSettings[GLOBAL_CHANNEL] : _multi[static_cast<size_t>(MD_PART_MIDI_CHANNEL) + _part];
		for (const auto cc : g_pageA)
		{
			SMidiEvent ev;
			ev.source = _source;
			ev.a = M_CONTROLCHANGE + channel;
			ev.b = cc;
			ev.c = _dump[cc];
			_responses.emplace_back(std::move(ev));
		}
		for (const auto cc : g_pageB)
		{
			SMidiEvent ev;
			ev.source = _source;
			ev.a = M_POLYPRESSURE + channel;
			ev.b = cc;
			ev.c = _dump[static_cast<size_t>(cc)+128];
			_responses.emplace_back(std::move(ev));
		}
		
	};

	switch (cmd)
	{
		case DUMP_SINGLE: 
			{
				const auto bank = fromMidiByte(_data[7]);
				const uint8_t program = _data[8];
				LOG("Received Single dump, Bank " << (int)toMidiByte(bank) << ", program " << (int)program);
				TPreset preset;
				preset.fill(0);
				if(_data.size() == 524 && m_rom.isTIFamily())
				{
					// D preset
					auto data(_data);

					data.erase(data.begin() + 0x100 + g_sysexPresetHeaderSize);	// A/B/C checksum, not needed on D
					std::copy_n(data.data() + g_sysexPresetHeaderSize, std::min(preset.size(), _data.size() - g_sysexPresetHeaderSize - g_sysexPresetFooterSize), preset.begin());
				}
				else
				{
					std::copy_n(_data.data() + g_sysexPresetHeaderSize, std::min(preset.size(), _data.size() - g_sysexPresetHeaderSize - g_sysexPresetFooterSize), preset.begin());
				}
				return writeSingle(bank, program, preset);
			}
		case DUMP_MULTI:
			{
				const auto bank = fromMidiByte(_data[7]);
				const uint8_t program = _data[8];
				LOG("Received Multi dump, Bank " << (int)toMidiByte(bank) << ", program " << (int)program);
				TPreset preset;
				std::copy_n(_data.data() + g_sysexPresetHeaderSize, std::min(preset.size(), _data.size() - g_sysexPresetHeaderSize - g_sysexPresetFooterSize), preset.begin());
				return writeMulti(bank, program, preset);
			}
		case REQUEST_SINGLE:
			{
				const auto bank = fromMidiByte(_data[7]);
				const uint8_t program = _data[8];
				LOG("Request Single, Bank " << (int)toMidiByte(bank) << ", program " << (int)program);
				buildSingleResponse(bank, program);
				break;
			}
		case REQUEST_MULTI:
			{
				const auto bank = fromMidiByte(_data[7]);
				const uint8_t program = _data[8];
				LOG("Request Multi, Bank " << (int)bank << ", program " << (int)program);
				buildMultiResponse(bank, program);
				break;
			}
		case REQUEST_BANK_SINGLE:
			{
				const auto bank = fromMidiByte(_data[7]);
				buildSingleBankResponse(bank);
				break;
			}
		case REQUEST_BANK_MULTI:
			{
				const auto bank = fromMidiByte(_data[7]);
				buildMultiBankResponse(bank);
				break;
			}
		case REQUEST_CONTROLLER_DUMP:
			{
				const auto part = _data[8];
				if (part < 16 || part == SINGLE)
					buildControllerDumpResponse(part);
				break;
			}
		case REQUEST_GLOBAL:
			buildGlobalResponses();
			break;
		case REQUEST_TOTAL:
			buildTotalResponse();
			break;
		case REQUEST_ARRANGEMENT:
			buildArrangementResponse();
			break;
		case PARAM_CHANGE_A:
		case PARAM_CHANGE_B:
		case PARAM_CHANGE_C:
			{
				const auto page = static_cast<Page>(cmd);

				const auto part = _data[7];
				const auto param = _data[8];
				const auto value = _data[9];

				if(page == PAGE_C || (page == PAGE_B && param == CLOCK_TEMPO))
				{
					applyToMultiEditBuffer(part, param, value);

					const auto command = static_cast<ControlCommand>(param);

					switch(command)
					{
					case PLAY_MODE:
						{
							const auto playMode = value;

							switch(playMode)
							{
							case PlayModeSingle:
								{
									LOG("Switch to Single mode");
									return writeSingle(BankNumber::EditBuffer, SINGLE, m_singleEditBuffer);
								}
							case PlayModeMultiSingle:
							case PlayModeMulti:
								{
									writeMulti(BankNumber::EditBuffer, 0, m_multiEditBuffer);
									for(uint8_t i=0; i<16; ++i)
										writeSingle(BankNumber::EditBuffer, i, m_singleEditBuffers[i]);
									return true;
								}
							default:
								return true;
							}
						}
					case PART_BANK_SELECT:
						return partBankSelect(part, value, false);
					case PART_BANK_CHANGE:
						return partBankSelect(part, value, true);
					case PART_PROGRAM_CHANGE:
						return partProgramChange(part, value);
					case MULTI_PROGRAM_CHANGE:
						if(part == 0)
						{
							return multiProgramChange(value);
						}
						return true;
					default:
						break;
					}
				}
				else
				{
					if (m_globalSettings[PLAY_MODE] != PlayModeSingle || part == SINGLE)
					{
						// virus only applies sysex changes to other parts while in multi mode.
						applyToSingleEditBuffer(page, part, param, value);
					}
					if (m_globalSettings[PLAY_MODE] == PlayModeSingle && part == 0)
					{
						// accept parameter changes in single mode even if sent for part 0, this is how the editor does it right now
						applyToSingleEditBuffer(page, SINGLE, param, value);
					}
				}

				// bounce back to UI if not sent by editor
				if(_source != MidiEventSourceEditor)
				{
					SMidiEvent ev;
					ev.sysex = _data;
					ev.source = MidiEventSourceEditor;	// don't send to output
					_responses.push_back(ev);
				}

				return send(page, part, param, value);
			}
		default:
			LOG("Unknown sysex command " << HEXN(cmd, 2));
	}

	return true;
}

std::vector<TWord> Microcontroller::presetToDSPWords(const TPreset& _preset, const bool _isMulti) const
{
	const auto presetVersion = getPresetVersion(_preset);
	const auto presetModel = presetVersion <= C ? ROMFile::Model::ABC : ROMFile::Model::Snow;

	const auto targetByteSize = _isMulti ? m_rom.getMultiPresetSize() : m_rom.getSinglePresetSize();
	const auto sourceByteSize = _isMulti ? ROMFile::getMultiPresetSize(presetModel) : ROMFile::getSinglePresetSize(presetModel);

	const auto sourceWordSize = (sourceByteSize + 2) / 3;
	const auto targetWordSize = (targetByteSize + 2) / 3;

	std::vector<TWord> preset;
	preset.resize(targetWordSize, 0);

	size_t idx = 0;
	for (size_t i = 0; i < sourceWordSize && i < targetWordSize; i++)
	{
		if (i == (sourceWordSize - 1))
		{
			if (idx < sourceByteSize)
				preset[i] = _preset[idx] << 16;
			if ((idx + 1) < sourceByteSize)
				preset[i] |= _preset[idx + 1] << 8;
			if ((idx + 2) < sourceByteSize)
				preset[i] |= _preset[idx + 2];
		}
		else if (i < sourceWordSize)
		{
			preset[i] = ((_preset[idx] << 16) | (_preset[idx + 1] << 8) | _preset[idx + 2]);
		}

		idx += 3;
	}

	return preset;
}

bool Microcontroller::getSingle(BankNumber _bank, uint32_t _preset, TPreset& _result) const
{
	if (_bank == BankNumber::EditBuffer)
		return false;

	const auto bank = toArrayIndex(_bank);
	
	if(bank >= m_singles.size())
		return false;

	const auto& s = m_singles[bank];
	
	if(_preset >= s.size())
		return false;

	_result = s[_preset];
	return true;
}

bool Microcontroller::requestMulti(BankNumber _bank, uint8_t _program, TPreset& _data) const
{
	if (_bank == BankNumber::EditBuffer)
	{
		// Use multi-edit buffer
		_data = m_multiEditBuffer;
		return true;
	}

	if (_bank != BankNumber::A)
		return false;

	// Load from flash
	return m_rom.getMulti(_program, _data);
}

bool Microcontroller::requestSingle(BankNumber _bank, uint8_t _program, TPreset& _data) const
{
	if (_bank == BankNumber::EditBuffer)
	{
		// Use single-edit buffer
		if(_program == SINGLE)
			_data = m_singleEditBuffer;
		else
			_data = m_singleEditBuffers[_program % m_singleEditBuffers.size()];

		return true;
	}

	// Load from flash
	return getSingle(_bank, _program, _data);
}

bool Microcontroller::writeSingle(BankNumber _bank, uint8_t _program, const TPreset& _data)
{
	if (_bank != BankNumber::EditBuffer) 
	{
		const auto bank = toArrayIndex(_bank);

		if(bank >= m_singles.size() || bank >= g_singleRamBankCount)
			return true;	// out of range

		if(_program >= m_singles[bank].size())
			return true;	// out of range

		m_singles[bank][_program] = _data;

		return true;
	}

	if(_program == SINGLE)
		m_singleEditBuffer = _data;
	else
		m_singleEditBuffers[_program % m_singleEditBuffers.size()] = _data;

	LOG("Loading Single " << ROMFile::getSingleName(_data) << " to part " << static_cast<int>(_program));

	if(_program == SINGLE)
		m_globalSettings[PLAY_MODE] = PlayModeSingle;

	// Send to DSP
	return sendPreset(_program, presetToDSPWords(_data, false), false);
}

bool Microcontroller::writeMulti(BankNumber _bank, uint8_t _program, const TPreset& _data)
{
	if (_bank != BankNumber::EditBuffer) 
	{
		LOG("We do not support writing to RAM or ROM, attempt to write multi to bank " << static_cast<int>(toMidiByte(_bank)) << ", program " << static_cast<int>(_program));
		return true;
	}

	m_multiEditBuffer = _data;

	LOG("Loading Multi " << ROMFile::getMultiName(_data));

	m_globalSettings[PLAY_MODE] = PlayModeMulti;

	// Convert array of uint8_t to vector of 24bit TWord
	return sendPreset(_program, presetToDSPWords(_data, true), true);
}

bool Microcontroller::partBankSelect(const uint8_t _part, const uint8_t _value, const bool _immediatelySelectSingle)
{
	if(_part == SINGLE)
	{
		const auto bankIndex = static_cast<uint8_t>(toArrayIndex(fromMidiByte(_value)) % m_singles.size());
		m_currentBank = bankIndex;
		return true;
	}

	m_multiEditBuffer[MD_PART_BANK_NUMBER + _part] = _value;

	if(_immediatelySelectSingle)
		return partProgramChange(_part, m_multiEditBuffer[MD_PART_PROGRAM_NUMBER + _part]);

	return true;
}

bool Microcontroller::partProgramChange(const uint8_t _part, const uint8_t _value)
{
	TPreset single;

	if(_part == SINGLE)
	{
		if (getSingle(fromArrayIndex(m_currentBank), _value, single))
		{
			m_currentSingle = _value;
			return writeSingle(BankNumber::EditBuffer, SINGLE, single);
		}
		return false;
	}

	const auto bank = fromMidiByte(m_multiEditBuffer[MD_PART_BANK_NUMBER + _part]);

	if(getSingle(bank, _value, single))
	{
		m_multiEditBuffer[MD_PART_PROGRAM_NUMBER + _part] = _value;
		return writeSingle(BankNumber::EditBuffer, _part, single);
	}

	return true;
}

bool Microcontroller::multiProgramChange(uint8_t _value)
{
	TPreset multi;

	if(!m_rom.getMulti(_value, multi))
		return true;

	return loadMulti(_value, multi);
}

bool Microcontroller::loadMulti(uint8_t _program, const TPreset& _multi)
{
	if(!writeMulti(BankNumber::EditBuffer, _program, _multi))
		return false;

	for (uint8_t p = 0; p < 16; ++p)
		loadMultiSingle(p, _multi);

	return true;
}

bool Microcontroller::loadMultiSingle(uint8_t _part)
{
	return loadMultiSingle(_part, m_multiEditBuffer);
}

bool Microcontroller::loadMultiSingle(uint8_t _part, const TPreset& _multi)
{
	const auto partBank = _multi[MD_PART_BANK_NUMBER + _part];
	const auto partSingle = _multi[MD_PART_PROGRAM_NUMBER + _part];

	partBankSelect(_part, partBank, false);
	return partProgramChange(_part, partSingle);
}

void Microcontroller::process(size_t _size)
{
	std::lock_guard lock(m_mutex);

	if(m_pendingPresetWriteDelay > 0)
	{
		m_pendingPresetWriteDelay -= static_cast<int>(_size);

		if(m_pendingPresetWriteDelay > 0)
			return;
	}

	if(!m_pendingPresetWrites.empty() && !m_hdi08.hasDataToSend())
	{
		const auto preset = m_pendingPresetWrites.front();
		m_pendingPresetWrites.pop_front();

		sendPreset(preset.program, preset.data, preset.isMulti);
	}
}

bool Microcontroller::getState(std::vector<unsigned char>& _state, const StateType _type)
{
	const auto deviceId = m_globalSettings[DEVICE_ID];

	std::vector<SMidiEvent> responses;

	if(_type == StateTypeGlobal)
		sendSysex({M_STARTOFSYSEX, 0x00, 0x20, 0x33, 0x01, deviceId, REQUEST_TOTAL, M_ENDOFSYSEX}, responses, MidiEventSourcePlugin);

	sendSysex({M_STARTOFSYSEX, 0x00, 0x20, 0x33, 0x01, deviceId, REQUEST_ARRANGEMENT, M_ENDOFSYSEX}, responses, MidiEventSourcePlugin);

	if(responses.empty())
		return false;

	for (const auto& response : responses)
	{
		assert(!response.sysex.empty());
		_state.insert(_state.end(), response.sysex.begin(), response.sysex.end());		
	}

	return true;
}

bool Microcontroller::setState(const std::vector<unsigned char>& _state, const StateType _type)
{
	std::vector<SMidiEvent> events;

	for(size_t i=0; i<_state.size(); ++i)
	{
		if(_state[i] == 0xf0)
		{
			const auto begin = i;

			for(++i; i<_state.size(); ++i)
			{
				if(_state[i] == 0xf7)
				{
					SMidiEvent ev;
					ev.sysex.resize(i + 1 - begin);
					memcpy(&ev.sysex[0], &_state[begin], ev.sysex.size());
					events.emplace_back(ev);
					break;
				}
			}
		}
	}

	if(events.empty())
		return false;

	// delay all preset loads until everything is loaded
	m_loadingState = true;

	std::vector<SMidiEvent> unusedResponses;

	for (const auto& event : events)
	{
		sendSysex(event.sysex, unusedResponses, MidiEventSourcePlugin);
		unusedResponses.clear();
	}

	m_loadingState = false;

	return true;
}

bool Microcontroller::sendMIDItoDSP(uint8_t _a, const uint8_t _b, const uint8_t _c) const
{
	std::lock_guard lock(m_mutex);

	const auto isModelD = m_rom.isTIFamily();

	const char flagA = isModelD ? 0 : 1;

	writeHostBitsWithWait(flagA, 1);

	auto sendMIDItoDSP = [this, isModelD](const uint8_t _midiByte)
	{
		const TWord word = static_cast<TWord>(_midiByte) << 16 | (isModelD ? 0xffff : 0);
		m_hdi08.writeRX(&word, 1);
	};

	const auto command = (_a & 0xf0);

	if(command == 0xf0)
	{
		// single-byte status message
		sendMIDItoDSP(_a);
	}
	else
	{
		sendMIDItoDSP(_a);
		sendMIDItoDSP(_b);

		if(command != M_AFTERTOUCH)
			sendMIDItoDSP(_c);
	}

	return true;
}

void Microcontroller::sendPendingMidiEvents(const uint32_t _maxOffset)
{
	auto size = m_pendingMidiEvents.size();

	if(!size)
		return;

	while(!m_pendingMidiEvents.empty() && m_pendingMidiEvents.front().offset <= _maxOffset)
	{
		const auto& ev = m_pendingMidiEvents.front();

		if(!sendMIDItoDSP(ev.a,ev.b,ev.c))
			break;

		m_pendingMidiEvents.pop_front();
		--size;
	}	
}

PresetVersion Microcontroller::getPresetVersion(const TPreset& _preset)
{
	return getPresetVersion(_preset[0]);
}

PresetVersion Microcontroller::getPresetVersion(const uint8_t _versionCode)
{
	return static_cast<PresetVersion>(_versionCode);
}

void Microcontroller::applyToSingleEditBuffer(const Page _page, const uint8_t _part, const uint8_t _param, const uint8_t _value)
{
	if(_part == SINGLE)
		applyToSingleEditBuffer(m_singleEditBuffer, _page, _param, _value);
	else
		applyToSingleEditBuffer(m_singleEditBuffers[_part], _page, _param, _value);
}

void Microcontroller::applyToSingleEditBuffer(TPreset& _single, const Page _page, const uint8_t _param, const uint8_t _value) const
{
	const uint32_t offset = (_page - PAGE_A) * m_rom.getPresetsPerBank() + _param;

	if(offset >= _single.size())
		return;

	_single[offset] = _value;
}

void Microcontroller::applyToMultiEditBuffer(const uint8_t _part, const uint8_t _param, const uint8_t _value)
{
	// remap page C parameters into the multi edit buffer
	if (_param >= PART_MIDI_CHANNEL && _param <= PART_OUTPUT_SELECT) {
		m_multiEditBuffer[MD_PART_MIDI_CHANNEL + ((_param-PART_MIDI_CHANNEL)*16) + _part] = _value;
	}
	else if (_param == CLOCK_TEMPO) {
		m_multiEditBuffer[MD_CLOCK_TEMPO] = _value;
	}
}

}
