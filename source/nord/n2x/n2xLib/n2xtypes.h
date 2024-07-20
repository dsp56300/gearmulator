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

	static constexpr uint32_t g_dspAAddress				= 0x200008;
	static constexpr uint32_t g_dspBAddress				= 0x200010;

	static constexpr uint32_t g_frontPanelAddressCS4	= 0x202800;
	static constexpr uint32_t g_frontPanelAddressCS6	= 0x202000;
	static constexpr uint32_t g_keyboardAddress			= 0x203000;

	static constexpr uint32_t g_frontPanelSize			= 0x800;
	static constexpr uint32_t g_keyboardSize			= 0x800;
}
