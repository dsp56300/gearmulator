#pragma once

#include <vector>
#include <cstdint>

namespace synthLib
{
	// MIDI status bytes
	enum MidiStatusByte
	{
		M_NOTEOFF			= 0x80,
		M_NOTEON			= 0x90,
		M_POLYPRESSURE		= 0xa0,
		M_CONTROLCHANGE		= 0xb0,
		M_PROGRAMCHANGE		= 0xc0,
		M_AFTERTOUCH		= 0xd0,
		M_PITCHBEND			= 0xe0,

		// single status bytes
		M_SONGPOSITION		= 0xf2,
		M_SONGSELECT		= 0xf3,
		M_TUNEREQUEST		= 0xf5,
		M_SYSTEMRESET		= 0xff,
		M_TIMINGCLOCK		= 0xf8,
		M_START				= 0xfa,
		M_CONTINUE			= 0xfb,
		M_STOP				= 0xfc,
		M_ACTIVESENSING		= 0xfe,
		M_STARTOFSYSEX		= 0xf0,
		M_ENDOFSYSEX		= 0xf7,

		// send as data byte 1 for control changes
		M_LOCALOFF			= 0x7a,
		M_ALLNOTESOFF		= 0x7b,
		M_OMNIMODEOFF		= 0x7c,
		M_OMNIMODEON		= 0x7d,
		M_MONOMODEON		= 0x7e,
		M_POLYMODEON		= 0x7f
	};

	// control changes

	enum
	{
		MC_BANKSELECTMSB,			// 0
		MC_MODULATION,				// 1
		MC_BREATHCONTROL,			// 2
		MC_CONTROL3,				// 3
		MC_FOOTCONTROL,				// 4
		MC_PORTAMENTOTIME,			// 5
		MC_DATAENTRYMSB,			// 6
		MC_MAINVOLUME,				// 7
		MC_BALANCE,					// 8
		MC_CONTROL9,				// 9
		MC_PAN,						// 10
		MC_EXPRESSION,				// 11
		MC_CONTROL12,				// 12
		MC_CONTROL13,				// 13
		MC_CONTROL14,				// 14
		MC_CONTROL15,				// 15
		MC_GENERALPURPOSE1,			// 16
		MC_GENERALPURPOSE2,			// 17
		MC_GENERALPURPOSE3,			// 18
		MC_GENERALPURPOSE4,			// 19
		MC_CONTROL20,				// 20
		MC_CONTROL21,				// 21
		MC_CONTROL22,				// 22
		MC_CONTROL23,				// 23
		MC_CONTROL24,				// 24
		MC_CONTROL25,				// 25
		MC_CONTROL26,				// 26
		MC_CONTROL27,				// 27
		MC_CONTROL28,				// 28
		MC_CONTROL29,				// 29
		MC_CONTROL30,				// 30
		MC_CONTROL31,				// 31
		MC_BANKSELECTLSB,			// 32
		MC_MODULATIONLSB,			// 33
		MC_BREATHCONTROLLSB,		// 34
		MC_CONTROL35,				// 35
		MC_FOOTCONTROLLSB,			// 36
		MC_PORTAMENTOTIMELSB,		// 37
		MC_DATAENTRYLSB,			// 38
		MC_MAINVOLUMELSB,			// 39
		MC_BALANCELSB,				// 40
		MC_CONTROL41,				// 41
		MC_PANLSB,					// 42
		MC_EXPRESSIONLSB,			// 43
		MC_CONTROL44,				// 44
		MC_CONTROL45,				// 45
		MC_CONTROL46,				// 46
		MC_CONTROL47,				// 47
		MC_GENERALPURPOSE1LSB,		// 48
		MC_GENERALPURPOSE2LSB,		// 49
		MC_GENERALPURPOSE3LSB,		// 50
		MC_GENERALPURPOSE4LSB,		// 51
		MC_CONTROL52,				// 52
		MC_CONTROL53,				// 53
		MC_CONTROL54,				// 54
		MC_CONTROL55,				// 55
		MC_CONTROL56,				// 56
		MC_CONTROL57,				// 57
		MC_CONTROL58,				// 58
		MC_CONTROL59,				// 59
		MC_CONTROL60,				// 60
		MC_CONTROL61,				// 61
		MC_CONTROL62,				// 62
		MC_CONTROL63,				// 63
		MC_SUSTAINPEDAL,			// 64
		MC_PORTAMENTOPEDAL,			// 65
		MC_SOSTENUTOPEDAL,			// 66
		MC_SOFTPEDAL,				// 67
		MC_LEGATOFOOTSWITCH,		// 68
		MC_HOLDPEDAL2,				// 69
		MC_VARIATION_EXCITER,		// 70
		MC_HARMONIC_COMPRESSOR,		// 71
		MC_RELEASETIME_DISTORTION,	// 72
		MC_ATTACKTIME_EQUALIZER,	// 73
		MC_EXPANDER_NOISEGATE,		// 74
		MC_UNDEFINEDREVERB,			// 75
		MC_UNDEFINEDDELAY,			// 76
		MC_UNDEFINED_PITCHSHIFT,	// 77
		MC_UNDEFINED_FLANGER_CHORUS,// 78
		MC_UNDEFINED_SPECIALEFFECTS,// 79
		MC_GENERALPURPOSE5,			// 80
		MC_GENERALPURPOSE6,			// 81
		MC_GENERALPURPOSE7,			// 82
		MC_GENERALPURPOSE8,			// 83
		MC_PORTAMENTOCONTROL,		// 84
		MC_CONTROL85,				// 85
		MC_CONTROL86,				// 86
		MC_CONTROL87,				// 87
		MC_CONTROL88,				// 88
		MC_CONTROL89,				// 89
		MC_CONTROL90,				// 90
		MC_EFFECTSDEPTH,			// 91
		MC_TREMOLODEPTH,			// 92
		MC_CHORUSDEPTH,				// 93
		MC_DETUNE,					// 94
		MC_PHASERDEPTH,				// 95
		MC_DATAINCREMENT0,			// 96
		MC_DATAINCREMENT1,			// 97
		MC_NRPNLSB,					// 98
		MC_NRPNMSB,					// 99
		MC_RPNLSB,					// 100
		MC_RPNMSB,					// 101
		MC_CONTROL102,				// 102
		MC_CONTROL103,				// 103
		MC_CONTROL104,				// 104
		MC_CONTROL105,				// 105
		MC_CONTROL106,				// 106
		MC_CONTROL107,				// 107
		MC_CONTROL108,				// 108
		MC_CONTROL109,				// 109
		MC_CONTROL110,				// 110
		MC_CONTROL111,				// 111
		MC_CONTROL112,				// 112
		MC_CONTROL113,				// 113
		MC_CONTROL114,				// 114
		MC_CONTROL115,				// 115
		MC_CONTROL116,				// 116
		MC_CONTROL117,				// 117
		MC_CONTROL118,				// 118
		MC_CONTROL119,				// 119
		MC_ALLSOUNDOFF,				// 120
		MC_RESETALLCONTROLLERS,		// 121
		MC_LOCALCONTROL,			// 122
		MC_ALLNOTESOFF,				// 123
		MC_OMNIMODEOFF,				// 124
		MC_OMNIMODEON,				// 125
		MC_MONOMODEON,				// 126
		MC_POLYMODEON,				// 127

		M_NUMMIDICONTROLLERS
	};

	enum NoteNumbers
	{
		Note_Cm4 = -24, Note_Cism4, Note_Dm4, Note_Dism4, Note_Em4, Note_Fm4, Note_Fism4, Note_Gm4, Note_Gism4, Note_Am4, Note_Aism4, Note_Bm4,
		Note_Cm3 = -12, Note_Cism3, Note_Dm3, Note_Dism3, Note_Em3, Note_Fm3, Note_Fism3, Note_Gm3, Note_Gism3, Note_Am3, Note_Aism3, Note_Bm3,
		Note_Cm2 = 0, Note_Cism2, Note_Dm2, Note_Dism2, Note_Em2, Note_Fm2, Note_Fism2, Note_Gm2, Note_Gism2, Note_Am2, Note_Aism2, Note_Bm2,
		Note_Cm1, Note_Cism1, Note_Dm1, Note_Dism1, Note_Em1, Note_Fm1, Note_Fism1, Note_Gm1, Note_Gism1, Note_Am1, Note_Aism1, Note_Bm1,
		Note_C0, Note_Cis0, Note_D0, Note_Dis0, Note_E0, Note_F0, Note_Fis0, Note_G0, Note_Gis0, Note_A0, Note_Ais0, Note_B0,
		Note_C1, Note_Cis1, Note_D1, Note_Dis1, Note_E1, Note_F1, Note_Fis1, Note_G1, Note_Gis1, Note_A1, Note_Ais1, Note_B1,
		Note_C2, Note_Cis2, Note_D2, Note_Dis2, Note_E2, Note_F2, Note_Fis2, Note_G2, Note_Gis2, Note_A2, Note_Ais2, Note_B2,
		Note_C3, Note_Cis3, Note_D3, Note_Dis3, Note_E3, Note_F3, Note_Fis3, Note_G3, Note_Gis3, Note_A3, Note_Ais3, Note_B3,
		Note_C4, Note_Cis4, Note_D4, Note_Dis4, Note_E4, Note_F4, Note_Fis4, Note_G4, Note_Gis4, Note_A4, Note_Ais4, Note_B4,
		Note_C5, Note_Cis5, Note_D5, Note_Dis5, Note_E5, Note_F5, Note_Fis5, Note_G5, Note_Gis5, Note_A5, Note_Ais5, Note_B5,
		Note_C6, Note_Cis6, Note_D6, Note_Dis6, Note_E6, Note_F6, Note_Fis6, Note_G6, Note_Gis6, Note_A6, Note_Ais6, Note_B6,
		Note_C7, Note_Cis7, Note_D7, Note_Dis7, Note_E7, Note_F7, Note_Fis7, Note_G7, Note_Gis7, Note_A7, Note_Ais7, Note_B7,
		Note_C8, Note_Cis8, Note_D8, Note_Dis8, Note_E8, Note_F8, Note_Fis8, Note_G8
	};

	struct SMidiEvent
	{
		uint8_t a, b, c;
		std::vector<uint8_t> sysex;
		int offset;

		SMidiEvent(const uint8_t _a = 0, const uint8_t _b = 0, const uint8_t _c = 0, const int _offset = 0) : a(_a), b(_b), c(_c), offset(_offset) {}
	};
}
