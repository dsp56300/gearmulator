#pragma once

#include <cstdint>

namespace n2x
{
	/* CHIP SELECTS                                 START          SIZE
	CS0 = DSPs (both)                               $200000        $800
	CS1 = nc
	CS2 = nc
	CS3 = nc
	CS4 = front panel                               $202800        $800
	CS5 = keyboard via 74HC374 TSSOP (both)         $203000        $800
	CS6 = front panel                               $202000        $800
	CS7 = nc
	CS8 = RAM A                                     $100000        $40000
	CS9 = RAM B                                     $100000        $40000
	CS10 = RAMs (both)                              $100000        $40000
	CSBOOT = BootROM                                $??????        $?????
	*/

	static constexpr uint32_t g_romSize					= 1024 * 512;
	static constexpr uint32_t g_ramSize					= 1024 * 256;
	static constexpr uint32_t g_flashSize				= 1024 * 64;

	static constexpr uint32_t g_pcInitial				= 0xc2;

	static constexpr uint32_t g_romAddress				= 0;
	static constexpr uint32_t g_ramAddress				= 0x100000;

	static constexpr uint32_t g_dspBothAddress			= 0x200000;
	static constexpr uint32_t g_dspAAddress				= 0x200008;
	static constexpr uint32_t g_dspBAddress				= 0x200010;

	static constexpr uint32_t g_frontPanelAddressCS4	= 0x202800;
	static constexpr uint32_t g_frontPanelAddressCS6	= 0x202000;
	static constexpr uint32_t g_keyboardAddress			= 0x203000;

	static constexpr uint32_t g_frontPanelSize			= 0x800;
	static constexpr uint32_t g_keyboardSize			= 0x800;

	static constexpr uint32_t g_samplerate				= 98200;

	enum class ButtonType
	{
		// id: MSB = address / LSB = bit mask

		Trigger			= 0x00'01,
		OctPlus			= 0x00'02,
		OctMinus		= 0x00'04,
		Unused0008		= 0x00'08,
		Up				= 0x00'10,
		Down			= 0x00'20,
		Unused0040		= 0x00'40,
		Unused0080		= 0x00'80,

		Assign			= 0x02'01,
		Perf			= 0x02'02,
		Osc2Keytrack	= 0x02'04,
		OscSync			= 0x02'08,
		FilterType		= 0x02'10,
		FilterVelo		= 0x02'20,
		FilterDist		= 0x02'40,
		Osc2Shape		= 0x02'80,

		Distortion		= 0x04'01,
		Arp				= 0x04'02,
		Store			= 0x04'04,
		ModEnvDest		= 0x04'08,
		Lfo1Shape		= 0x04'10,
		Lfo1Dest		= 0x04'20,
		Lfo2Shape		= 0x04'40,
		Osc1Shape		= 0x04'80,

		SlotA			= 0x06'01,
		SlotB			= 0x06'02,
		SlotC			= 0x06'04,
		SlotD			= 0x06'08,
		Poly			= 0x06'10,
		Unison			= 0x06'20,
		Portamento		= 0x06'40,
		ModwheelDest	= 0x06'80,

		Shift = ModwheelDest
	};

	enum class KnobType
	{
		Invalid = 0,
		Osc1Fm    = 0x38,	Porta,		Lfo2Rate,	Lfo1Rate,		MasterVol,	ModEnvAmt,	ModEnvD,	ModEnvA,
		AmpEnvD   = 0x58,	FilterFreq,	FilterEnvA,	AmpEnvA,		OscMix,		Osc2Fine,	Lfo1Amount,	OscPW,
		AmpGain   = 0x68,	FilterEnvR,	AmpEnvR,	FilterEnvAmt,	FilterEnvS,	AmpEnvS,	FilterReso,	FilterEnvD,
		PitchBend = 0x70,	ModWheel,	ExpPedal,	Lfo2Amount,		Osc2Semi,

		First = Osc1Fm,
		Last = Osc2Semi
	};
}
