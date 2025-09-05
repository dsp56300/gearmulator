#pragma once

namespace jeLib
{
	enum AddressArea
	{
		System             = 0x00000000,
		PerformanceTemp    = 0x01000000,
		UserPatch          = 0x02000000, // User Patch (Patch U:A11 - U:B88)
		UserPerformance    = 0x03000000, // User Performance (Performance U:11 - U:88)
		MotionControlDataA = 0x09000000, // Motion Control Data (Motion Set A)
		MotionControlDataB = 0x0A000000, // Motion Control Data (Motion Set B)
	};
}
