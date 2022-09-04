#include "mqParameters.h"

#include <cassert>
#include <sstream>

#include "dsp56kEmu/logging.h"

// This is a raw extraction of the "SDAT" table from page 40ff of the "Waldof MIDI Implementations" pdf by Achim Gratz, taken from http://synth.stromeko.net/Downloads.html
// Parsed with ABBY Fine Reader, copied to OpenOffice, saved as csv with some manual OCR corrections
constexpr char g_sdatCsv[] =
"Sound;;;;;;;;;;;;\n"
"Idx;PAH PAL;;;;;sndv16;SNDV10;Description;Name;;;\n"
"0;00h 00h;;;;;01h;1;Version 1;Sound Format;;;\n"
"Oscillator;;;;;;;;;;;;\n"
"Osc1;;Osc2;;Osc3;;;;;;;;\n"
"Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;sndv16;SNDVﾗ0;Description;Name;;;\n"
"1;00h 01h;17;00h 11h;33;00h 21h;10h,1Ch,28h, 34h,40h,4Ch, 58h,64h,70h;16, 28, 40, 52, 64, 76, 88,100,112;128', 64', 32', 16', 8', 4', 2' 1' ﾗ';Octave;;;\n"
"2;00h 02h;18;00h 12h;34;00h 22h;34h::40h::4Ch;52::64::76;-12::0::+12;Semitone;;;\n"
"3;00h 03h;19;00h 13h;35;00h 23h;00h::40h::7Fh;0::64::127;-64::0::+63;Detune;;;\n"
"4;00h 04h;20;00h 14h;36;00h 24h;28h::40h::58h;40::64::88;-24::0::+24;Bend Range;;;\n"
"5;00h 05h;21;00h 15h;37;00h 25h;00h::40h::7Fh;0::64::127;-200%::0%::+196%;Keytrack;;;\n"
"6;00h 06h;22;00h 16h;38;00h 26h;00h::0Eh;0::14;Off, Osc1, Osc2, Osc3, Noise, Ext Left, Ext Right, Ext L+R, LFO1, LFO2, LFO3, Filter Env, Amp Env, Env 3, Env 4;FM Source;;;\n"
"7;00h 07h;23;00h 17h;39;00h 27h;00h::7Fh;0::127;;FM Amount;;;\n"
"8;00h 08h;24;00h 18h;;;00h::06h;0::6;Off, Pulse, Saw, Triangle, Sine, Alt1, Alt2;Shape;;;\n"
";;;;40;00h 28h;00h::04h;0::4;Off, Pulse, Saw, Triangle, Sine;Shape;;;\n"
"9;00h 09h;25;00h 19h;41;00h 29h;00h::7Fh;0::127;;Pulsewidth;;;\n"
"10;00h 0Ah;26;00h 1Ah;42;00h 2Ah;00h::0Dh;0::13;Off, LFO1, LFO1*MW, LFO2, LFO2*Prs, LFO3, FilterEnv, AmpEnv, Env3, Env4, Velocity, ModWheel, Pitchbend, Pressure;PWM Source;;;\n"
"11;00h 0Bh;27;00h 1Bh;43;00h 2Bh;00h::40h::7Fh;0::64::127;-64::0::+63;PWM;;;\n"
"12;00h 0Ch;28;00h 1Ch;;;00h::1Fh;0::31;1::32;Sub Freq Div;;;\n"
"13;00h 0Dh;29;00h 1Dh;;;00h::7Fh;0::127;;Sub Volume;;;\n"
"Sync;;;;;;;;;;;;\n"
"Idx;PAH PAL;;;;;sndv16;SNDV10;Description;Name;;;\n"
"49;00h 31h;;;;;00h::01h;;Off, On;Enable;;;\n"
"Pitc;hMod;;;;;;;;;;;\n"
"Idx;PAH PAL;;;;;sndv16;SNDV10;Description;Name;;;\n"
"50;00h 32h;;;;;00h::0Dh;0::13;Off, LFO1, LFO1*MW, LFO2, LFO2*Prs, LFO3, FilterEnv, AmpEnv, Env3, Env4, Velocity, ModWheel, Pitchbend, Pressure;Source;;;\n"
"51;00h 33h;;;;;00h::40h::7Fh;0::64::127;-64::0::+63;Amount;;;\n"
"Glide;;;;;;;;;;;;\n"
"Idx;PAH PAL;;;;;sndv16;SNDV10;Description;Name;;;\n"
"53;00h 35h;;;;;00h, 01h;0, 1;Off, On;Active;;;\n"
"56;00h 38h;;;;;00h, 01h, 02h, 04h;0, 1, 2, 4;Portamento, Fingd. Portamento, Glissando, Fingd. Glissando;Mode;;;\n"
"57;00h 39h;;;;;00h::7Fh;0::127;;Rate;;;\n"
"Sound;;;;;;;;;;;;\n"
"Idx;PAH PAL;;;;;sndv16;SNDV10;Description;Name;;;\n"
"58;00h 3Ah;;;;;00h::05h;0, 1 0, 1,2::5;Poly, Mono Off,Dual,3::6;Voice Mode Unisono Count;;;\n"
"59;00h 3Bh;;;;;00h::7Fh;0::127;;Unisono Detune;;;\n"
"Mixer;;;;;;;;;;;;\n"
"Osc1;;Osc2;;Osc3;;;;;;;;\n"
"Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;sndv16;SNDV10;Description;Name;;;\n"
"61;00h 3Dh;63;00h 3Fh;65;00h 41h;00h::7Fh;0::127;;Level;;;\n"
"62;00h 3Eh;64;00h 40h;66;00h 42h;00h::40h::7Fh;0::64::127;F1 64::Mid::F2 63;Balance;;;\n"
"Noise/Extln;;Ring;Mod;;;;;;;;;\n"
"Idx;PAH PAL;Idx;PAH PAL;;;sndv16;SNDV10;Description;Name;;;\n"
"67;00h 43h;71;00h 47h;;;00h::7Fh;0::127;;Mix Level;;;\n"
"68;00h 44h;72;00h 48h;;;00h::40h::7Fh;0::64::127;F1 64::Mid::F2 63;Balance;;;\n"
"75;00h 4Bh;;;;;00h::03h;0::3;Noise,Ext Left, Ex Right, Ext L+R;Select F1;;;\n"
"76;00h 4Ch;;;;;00h::03h;0::3;Noise,Ext Left, Ex Right, Ext L+R;Select F2;;;\n"
";;;;;;;;;;;;\n"
"Filter;;;;;;;;;;;;\n"
"Filter 1;;Filter 2;;;;;;;;;;\n"
"Idx;PAH PAL;Idx;PAH PAL;;;;;sndv16;;SNDV10;Description;Name\n"
"77;00h 4Dh;97;00h 61h;;;;;00h, 01h, 02h, 03h, 04h, 05h, 06h, 07h, 08h, 09h, 0Ah;;0, 1,2, 3, 4, 5, 6, 7, 8, 9,10;Bypass, 24dB LP, 12dB LP, 24dB BP, 12dB BP, 24dB HP, 12dB HP, 24dB Notch, 12dB Notch, Comb+, Comb-;Type\n"
"78;00h 4Eh;98;00h 62h;;;;;00h::7Fh;0::127;;Cutoff;\n"
"80;00h 50h;100;00h 64h;;;;;00h::7Fh;0::127;;Resonance;\n"
"81;00h 51h;101;00h 65h;;;;;00h::7Fh;0::127;;Drive;\n"
"86;00h 56h;106;00h 6Ah;;;;;00h::40h::7Fh;0::64::127;-200%::0%::+196%;Keytrack;\n"
"87;00h 57h;107;00h 6Bh;;;;;00h::40h::7Fh;0::64::127;-64::0::+63;Envelope Modulation;\n"
"88;00h 58h;108;00h 6Ch;;;;;00h::40h::7Fh;0::64::127;-64::0::+63;Velocity Modulation;\n"
"89;00h 59h;109;00h 6Dh;;;;;00h::0Dh;0::13;Off, LFO1, LFO1*MW, LFO2, LFO2*Prs, LFO3, FilterEnv, AmpEnv, Env3, Env4, Velocity, ModWheel, Pitchbend, Pressure;Modulation Source;\n"
"90;00h 5Ah;110;00h 6Eh;;;;;00h::40h::7Fh;0::64::127;-63::0::+63;Cutoff Modulation\n"
"91;00h 5Bh;111;00h 6Fh;;;;;00h::0Eh;0::14;Off, Osc1, Osc2, Osc3, Noise, Ext Left, Ext Right, Ext L+R, LFO1, LFO2, LFO3, Filter Env, Amp Env, Env 3, Env 4;FM Source\n"
"92;00h 5Ch;112;00h 70h;;;;;00h::7Fh;0::127;Off, 1::127;FM Amount;\n"
"93;00h 5Dh;113;00h 71h;;;;;00h::40h::7Fh;0::64::127;Left 64::Center::Right 63;Pan;\n"
"94;00h 5Eh;114;00h 72h;;;;;00h::0Dh;0::13;Off, LFO1, LFO1*MW, LFO2, LFO2*Prs, LFO3, FilterEnv, AmpEnv, Env3, Env4, Velocity, ModWheel, Pitchbend, Pressure;Pan Mod Source;\n"
"95;00h 5Fh;115;00h 73h;;;;;00h::40h::7Fh;0::64::127;-64::0::+63;Pan Modulation\n"
"Filter;;;;;;;;;;;;\n"
"Idx;PAH PAL;;;;;;;sndv16;;SNDV10;Description;Name\n"
"117;00h 75h;;;;;;;00h, 01h;;0, 1;parallel, serial;Routing\n"
"Amp;;;;;;;;;;;;\n"
"Idx;PAH PAL;;;;;;;sndv16;;SNDV:o;Description;Name\n"
"121;00h 79h;;;;;;;00h::7Fh;0::127;;Volume;\n"
"122;00h 7Ah;;;;;;;00h::40h::7Fh;0::64::127;-64::0::+63;Velocity;\n"
"123;00h 7Bh;;;;;;;00h::0Dh;0::13;Off, LFO1, LFO1*MW, LFO2, LFO2*Prs, LFO3, FilterEnv, AmpEnv, Env3, Env4, Velocity, ModWheel, Pitchbend, Pressure;Modulation Source;\n"
"124;00h 7Ch;;;;;;;00h::40h::7Fh;0::64::127;-64::0::+63;Modulation Amount\n"
"Effects;;;;;;;;;;;;\n"
"FX1;;FX2;;;;;;;;;;\n"
"Idx;PAH PAL;Idx;PAH PAL;;;;;sndv16;;SNDV10;Description;Name\n"
"128;01h 00h;144;01h 10h;;;;;00h::06h;;0::6;Bypass, Chorus, Flanger, Phaser, Overdrive, Five FX, Vocoder;Effect\n"
";;144;01h 10h;;;;;07h::0Ah;;7::10;Delay, Reverb, 5.1 Delay, 5.1 D.Clk;Effect FX2 only\n"
"129;01h 01h;145;01h 11h;;;;;00h::7Fh;;0::127;Dry::Wet;Mix\n"
"Chorus FX1;;Chorus FX2;;Flanger FX1;;Flanger FX2;;;;;;\n"
"Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;sndv16;;SNDV:o;Description;Name\n"
"130;01h 02h;146;01h 12h;130;01h 02h;146;01h 12h;00h::7Fh;0::127;;Speed;\n"
"131;01h 03h;147;01h 13h;131;01h 03h;147;01h 13h;00h::7Fh;0::127;;Depth;\n"
"133;01h 05h;149;01h 15h;;;;;00h::7Fh;0::127;;Delay;\n"
";;;;134;01h 06h;150;01h 16h;00h::7Fh;0::127;0%::100%;Feedback;\n"
";;;;138;01h 0Ah;154;01h 1Ah;00h,01h;;0, 1;Positive, Negative;Polarity\n"
"Phaser FX1;;Phaser FX2;;Delay FX2;;;;;;;;\n"
"Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;;;sndv16;;SNDV10;Description;Name\n"
"130;01h 02h;146;01h 12h;;;;;00h::7Fh;0::127;;Speed;\n"
"131;01h 03h;147;01h 13h;;;;;00h::7Fh;0::127;;Depth;\n"
"134;01h 06h;150;01h 16h;150;01h 16h;;;00h::7Fh;0::127;0%::100%;Feedback;\n"
"135;01h 07h;151;01h 17h;;;;;00h::7Fh;0::127;;Center;\n"
";;;;151;01h 17h;;;00h::7Fh;0::127;;Cutoff;\n"
"136;01h 08h;152;01h 18h;;;;;00h::7Fh;0::127;;Spacing;\n"
"138;01h 0Ah;154;01h 1Ah;154;01h 1Ah;;;00h::01h;0, 1;Positive, Negative;Polarity;\n"
";;;;155;01h 1Bh;;;00h::01h;0, 1;Off, On;Autopan;\n"
"Overdrive FX1;;Overdrive FX2;;;;;;;;;;\n"
"Idx;PAH PAL;Idx;PAH PAL;;;;;sndv16;;SNDV10;Description;Name\n"
"131;01h 03h;147;01h 13h;;;;;00h::7Fh;0::127;;Drive;\n"
"132;01h 04h;148;01h 14h;;;;;00h::7Fh;0::127;;Post Gain;\n"
"135;01h 07h;151;01h 17h;;;;;00h::7Fh;0::127;;Cutoff;\n"
"FiveFX FX1;;FiveFX FX2;;;;;;;;;;\n"
"Idx;PAH PAL;Idx;PAH PAL;;;;;sndv16;;SNDV10;Description;Name\n"
"130;01h 02h;146;01h 12h;;;;;00h::7Fh;1::127;;Chorus Speed;\n"
"131;01h 03h;147;01h 13h;;;;;00h::7Fh;0::127;;Chorus Depth;\n"
"132;01h 04h;148;01h 14h;;;;;00h::7Fh;0::127;;Delay;\n"
"133;01h 05h;149;01h 15h;;;;;00h::7Fh;0::127;;Chorus/Delay L;\n"
"134;01h 06h;150;01h 16h;;;;;00h::7Fh;0::127;44.1KHz::2.6Hz;Sample&Hold;\n"
"135;01h 07h;151;01h 17h;;;;;00h::7Fh;0::127;;Overdrive;\n"
"136;01h 08h;152;01h 18h;;;;;00h::08h;0::8;External, Aux, FX1::FX4,Main In, Sub1 In, Sub2 In;Ring Mod Source;\n"
"137;01h 09h;153;01h 19h;;;;;00h::7Fh;;0::127;;Ring Mod Level\n"
"Vocoder FX1;;Vocoder FX2;;;;;;;;;;\n"
"Idx;PAH PAL;Idx;PAH PAL;;;;;sndv16;SNDV10;Description;Name\n"
"130;01h 02h;146;01h 12h;;;;;00h::17h;2::25;;Bands\n"
"131;01h 03h;147;01h 13h;;;;;00h::08h;0::8;External, Aux, FX1::FX4,Main In, Sub1 In, Sub2 In;Analysis Signal\n"
"132;01h 04h;148;01h 14h;;;;;00h::7Fh;0::127;10.9Hz::16.7KHz;A. Lo Freq;\n"
"133;01h 05h;149;01h 15h;;;;;00h::7Fh;0::127;10.9Hz::16.7KHz;A. Hi Freq;\n"
"134;01h 06h;150;01h 16h;;;;;00h::40h::7Fh;0::64::127 -128::-32(x3), -34::0::31(x1), +35::+128(x3);S. Offset;;\n"
"135;01h 07h;151;01h 17h;;;;;00h::40h::7Fh;0::64::127 -128::-32(x3), -34::0::31(x1), +35::+128(x3);Hi Offset;\n"
"136;01h 08h;152;01h 18h;;;;;00h::40h::7Fh;0::64::127;-64::0::+63;Bandwidth;\n"
"137;01h 09h;153;01h 19h;;;;;00h::40h::7Fh;0::64::127;-64::0::+63;Resonance;\n"
"138;01h 0Ah;154;01h 1Ah;;;;;00h::7Fh;0::127;;Attack;\n"
"139;01h 0Bh;155;01h 1Bh;;;;;00h::7Fh;0::127;;Decay;\n"
"140;01h 0Ch;156;01h 1Ch;;;;;00h::40h::7Fh;0::64::127;-64::0::+63;EQ Low Level;\n"
"141;01h 0Dh;157;01h 1Dh;;;;;00h::18h;0::24;Band 1::Band 25;EQ Mid Band;\n"
"142;01h 0Eh;158;01h 1Eh;;;;;00h::40h::7Fh;0::64::127;-64::0::+63;EQ Mid Level;\n"
"143;01h 0Fh;159;01h 1Fh;;;;;00h::40h::7Fh;0::64::127;-64::0::+63;EQ High Level;\n"
";;;;;;;;;;;;\n"
"Reverb FX1;;Reverb FX2;;;;;;;;;;\n"
"Idx;PAH PAL;Idx;PAH PAL;;;;;sndv16;;SNDV10;Description;Name\n"
"130;01h 02h;146;01h 12h;;;;;00h::7Fh;0::127;3m::30m;Size;\n"
"131;01h 03h;147;01h 13h;;;;;00h::7Fh;0::127;;Shape;\n"
"132;01h 04h;148;01h 14h;;;;;00h::7Fh;0::127;;Decay;\n"
"133;01h 05h;149;01h 15h;;;;;00h::7Fh;0::127;0ms::300ms;Pre-Delay;\n"
"135;01h 07h;151;01h 17h;;;;;00h::7Fh;0::127;;Lowpass;\n"
"136;01h 08h;152;01h 18h;;;;;00h::7Fh;0::127;;Highpass;\n"
"137;01h 09h;153;01h 19h;;;;;00h::7Fh;0::127;;Diffusion;\n"
"138;01h 0Ah;154;01h 1Ah;;;;;00h::7Fh;0::127;;Damping;\n"
"5.1 Delay FX2;;5.1 Clk.Delay FX2;;;;;;;;;;\n"
"Idx;PAH PAL;Idx;PAH PAL;;;;;sndv16;;SNDV10;Description;Name\n"
"146;01h 12h;;;;;;;00h::7Fh;;0::127;1.4ms::1.48s;Delay\n"
";;146;01h 12h;;;;;00h::1Dh;;0::29;1/128, 1/128T, 1/128, 1/64, 1/64T, 1/64, 1/32, 1/32T, 1/32, 1/16, 1/16T, 1/16, 1/8, 1/8T, 1/8, 1/4, 1/4T, 1/4, 2/4, 2/4T, 2/4, 3/4, 3/4T, 4/4, 4/4, 4/4T, 4/4, 8/4, 8/4T, 8/4;Length\n"
"147;01h 13h;147;01h 13h;;;;;00h::7Fh;0::127;0%::100%;Feedback;\n"
"148;01h 14h;148;01h 14h;;;;;00h::7Fh;0::127;10.9Hz::16.7KHz;LFELP;\n"
"149;01h 15h;149;01h 15h;;;;;00h::7Fh;0::127;10.9Hz::16.7KHz;Input HP;\n"
"150;01h 16h;150;01h 16h;;;;;00h::7Fh;0::127;0%::400%;Delay ML;\n"
"151;01h 17h;151;01h 17h;;;;;00h::7Fh;0::127;;FSL Volume;\n"
"152;01h 18h;152;01h 18h;;;;;00h::7Fh;0::127;0%::400%;Delay MR;\n"
"153;01h 19h;153;01h 19h;;;;;00h::7Fh;0::127;;FSR Volume;\n"
"154;01h 1Ah;154;01h 1Ah;;;;;00h::7Fh;0::127;0%::400%;Delay S2L;\n"
"155;01h 1Bh;155;01h 1Bh;;;;;00h::7Fh;0::127;;CntrS Volume;\n"
"156;01h 1Ch;156;01h 1Ch;;;;;00h::7Fh;0::127;0%::400%;Delay S1L;\n"
"157;01h 1Dh;157;01h 1Dh;;;;;00h::7Fh;0::127;;RearSL Volume;\n"
"158;01h 1Eh;158;01h 1Eh;;;;;00h::7Fh;0::127;0%::400%;Delay S1R;\n"
"159;01h 1Fh;159;01h 1Fh;;;;;00h::7Fh;0::127;;RearSR Volume;\n"
"LFO;;;;;;;;;;;;\n"
"LFO1;;LFO2;;LFO3;;;;;;;;\n"
"Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;;;sndv16;;SNDV10;Description;Name\n"
"160;01h 20h;172;01h 2Ch;184;01h 38h;;;00h::05h;;0::5;Sine, Triangle, Square, Saw, Random, S&H;Shape\n"
"161;01h 21h;173;01h 2Dh;185;01h 39h;;;00h::7Fh;;0 0;0::127 256,192,160,144, 128, 120, 96, 80, 72, 64, 48, 40, 36, 32, 24, 20, 18, 16,15, 14,12,10, 9, 8, 7, 6, 5, 4, 3.5, 3, 2.66, 2.4, 2, 1.75, 1.5, 1.33, 1.2,1,7/8, 1/2., 1/2T, 5/8, 1/2, 7/16, 1/4., 1/4T, 5/16, 1/4, 7/32, 1/8., 1/8T, 5/32, 1/8, 7/64, 1/16., 1/16T, 5/64, 1/16, 1/32., 1/32T, 1/32, 1/64T, 1/64, 1/96 bars;Speed\n"
"163;01h 23h;175;01h 2Fh;187;01h 3Bh;;;00h, 01h;;0, 1;Off, On;Sync\n"
"164;01h 24h;176;01h 30h;188;01h 3Ch;;;00h, 01h;;0, 1;Off, On;Clocked\n"
"165;01h 25h;177;01h 31h;189;01h 3Dh;;;00h, 01h::7Fh;;0, 1::127;Free, 0::360;Start Phase\n"
"166;01h 26h;178;01h 32h;190;01h 3Eh;;;00h::7Fh;0::127;0::127;Delay;\n"
"167;01h 27h;179;01h 33h;191;01h 3Fh;;;00h::40h::7Fh;0::64::127;-64::0::+63;Fade;\n"
"170;01h 2Ah;182;01h 3 6h;194;01h 42h;;;00h::40h::7Fh;0::64::127;-200%::0%::+196%;Keytrack;\n"
"Envelopes;;;;;;;;;;;;\n"
"FiltEnv;;AmpEnv;;Env3;;Env4;;;;;;\n"
"Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;sndv16;;SNDV10;Description;Name\n"
"196;01h 44h;208;01h 50h;220;01h 5Ch;232;01h 68h;00h::04h;;0::4 0, 2;ADSR, ADS1DS2R, One Shot, Loop S1S2, Loop All Normal, Single;Env Mode Trigger Mode\n"
"199;01h 47h;211;01h 53h;223;01h 5Fh;235;01h 6Bh;00h::7Fh;0::127;0::127;Attack;\n"
"200;01h 48h;212;01h 54h;224;01h 60h;236;01h 6Ch;00h::7Fh;0::127;0::127;Attack Level;\n"
"201;01h 49h;213;01h 55h;225;01h 61h;237;01h 6Dh;00h::7Fh;0::127;0::127;Decay;\n"
"202;01h 4Ah;214;01h 5 6h;226;01h 62h;238;01h 6Eh;00h::7Fh;0::127;0::127;Sustain;\n"
"203;01h 4Bh;215;01h 57h;227;01h 63h;239;01h 6Fh;00h::7Fh;0::127;0::127;Decay 2;\n"
"204;01h 4Ch;216;01h 58h;228;01h 64h;240;01h 70h;00h::7Fh;0::127;0::127;Sustain 2;\n"
"205;01h 4Dh;217;01h 5 9h;229;01h 65h;241;01h 71h;00h::7Fh;0::127;0::127;Release;\n"
"Modifiers;;;;;;;;;;;;\n"
"Mod 1;;Mod 2;;Mod 3;;Mod 4;;;;;;\n"
"Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;sndv16;;SNDV10;Description;Name\n"
"245;01h 75h;249;01h 7 9h;253;01h 7Dh;257;02h 01h;00h::27h;0::39;Standard Mod Source;Source1;\n"
"246;01h 76h;250;01h 7Ah;254;01h 7Eh;258;02h 02h;00h::27h;0::39;Standard Mod Source;Source 2;\n"
"247;01h 77h;251;01h 7Bh;255;01h 7Fh;259;02h 03h;00h::07h;0::7;+, -, *, AND, OR, XOR, MAX, min;Operator;\n"
"248;01h 78h;252;01h 7Ch;256;02h 00h;260;02h 04h;00h::40h::7Fh;0::64::127;-64::0::+63;Constant;\n"
"Fast Mod Matrix;;;;;;;;;;;;\n"
"Slot 1F;;Slot 3F;;Slot 5F;;Slot 7F;;;;;;\n"
"Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;sndv16;;SNDV10;Description;Name\n"
"261;02h 05h;267;02h 0Bh;273;02h 11h;279;02h 17h;00h::0Dh;0::13;Fast Mod Source;Source;\n"
"262;02h 06h;268;02h 0Ch;274;02h 12h;280;02h 18h;00h::1Eh;0::30;Fast Mod Destination;Destination;\n"
"263;02h 07h;269;02h 0Dh;275;02h 13h;281;02h 19h;00h::40h::7Fh;0::64::127;-64::0::+63;Amount;\n"
"Fast Mod Matrix;;;;;;;;;;;;\n"
"Slot 2F;;Slot4F;;Slot 6F;;Slot8F;;;;;;\n"
"Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;sndv16;;SNDV10;Description;Name\n"
"264;02h 08h;270;02h 0Eh;276;02h 14h;282;02h 1Ah;00h::0Dh;0::13;Fast Mod Source;Source;\n"
"265;02h 09h;271;02h 0Fh;277;02h 15h;283;02h 1Bh;00h::1Eh;0::31;Fast Mod Destination;Destination;\n"
"266;02h 0Ah;272;02h 10h;278;02h 16h;284;02h 1Ch;00h::40h::7Fh;0::64::127;-64::0::+63;Amount;\n"
"Standard Mod Matrix;;;;;;;;;;;;\n"
"Slot 1S;;Slot 3S;;Slot 5S;;Slot 7S;;;;;;\n"
"Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;sndv16;;SNDV10;Description;Name\n"
"285;02h 1Dh;291;02h 23h;297;02h 29h;303;02h 2Fh;00h::27h;0::39;Standard Mod Source;Source;\n"
"286;02h 1Eh;292;02h 24h;298;02h 2Ah;304;02h 30h;00h::39h;0::57;Standard Mod Destination;Destination;\n"
"287;02h 1Fh;293;02h 25h;299;02h 2Bh;305;02h 31h;00h::40h::7Fh;0::64::127;-64::0::+63;Amount;\n"
"Standard Mod Matrix;;;;;;;;;;;;\n"
"Slot 2S;;Slot4S;;Slot 6S;;Slot8S;;;;;;\n"
"Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;sndv16;;SNDV10;Description;Name\n"
"288;02h 20h;294;02h 2 6h;300;02h 2Ch;306;02h 32h;00h::27h;0::39;Standard Mod Source;Source;\n"
"289;02h 21h;295;02h 27h;301;02h 2Dh;307;02h 33h;00h::39h;0::57;Standard Mod Destination;Destination;\n"
"290;02h 22h;296;02h 28h;302;02h 2Eh;308;02h 34h;00h::40h::7Fh;0::64::127;-64::0::+63;Amount;\n"
"Controller Delay;;;;;;;;;;;;\n"
"Idx;PAH PAL;;;;;;;sndv16;;SNDV10;Description;Name\n"
"309;02h 35h;;;;;;;00h::27h;;0::39;Standard Mod Source;Source\n"
"310;02h 36h;;;;;;;00h::7Fh;;0::127;;Ctr.Delay\n"
";;;;;;;;;;;;\n"
"Arp;;;;;;;;;;;;\n"
"Idx;PAH PAL;;;;;;;sndv16;SNDV10;Description;Name;\n"
"311;02h 37h;;;;;;;00h::03h;0::3;Off, On, One shot, Hold;Mode;\n"
"312;02h 38h;;;;;;;00h, 01h, 02h::10h;0, 1,2::16;Off, User, ROM1::ROM15;Pattern;\n"
"313;02h 39h;;;;;;;00h::0Fh;0::15;1::16;Max. Notes;\n"
"314;02h 3Ah;;;;;;;00h::7Fh;0::127;3/192::130/192;Clock;\n"
"315;02h 3Bh;;;;;;;00h, 01h::7Fh;0, 1::127;Legato, 1::127;Length;\n"
"316;02h 3Ch;;;;;;;00h::09h;0::9;1::10;Octave Range;\n"
"317;02h 3Dh;;;;;;;00h::03h;0::3;Up, Down, Alt Up, Alt Down;Direction;\n"
"318;02h 3Eh;;;;;;;00h::05h;0::5;As played, Reversed, NumLo^Hi, NumHi^Lo, VelLo^Hi, VelHi^Lo;Sort Order;\n"
"319;02h 3Fh;;;;;;;00h, 01h, 02h;0,1,2;Each note, First note, Last note;Velo Mode;\n"
"320;02h 40h;;;;;;;00h::7Fh;0::127;0::127;T. Factor;\n"
"321;02h 41h;;;;;;;00h::01h;0, 1;Off, On;Same note overlap;\n"
"322;02h 42h;;;;;;;00h::01h;0, 1;Off, On;Pattern Reset;\n"
"323;02h 43h;;;;;;;00h::0Fh;0::15;1::16;Pattern Length;\n"
"Tempo;;;;;;;;;;;;\n"
"Idx;PAH PAL;;;;;;;sndv16;SNDV10;Description;Name;\n"
"326;02h 46h;;;;;;;00h::7Fh;0::127;0::39, 40::90(2), 91::164, 165::300(5);Tempo (bpm);\n"
"Arp Step / Glide / Accent;;;;;;;;;;;;\n"
"Step 1-4;;Step 5-8;;Step9-12;;Step 13-16;;;;;;\n"
"Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;sndv16;SNDV10;Description;Name;\n"
"327;02h 47h;331;02h 4Bh;335;02h 4Fh;339;02h 53h;00h::7Fh;0::7 0, 1 0::7;*,, -, <, >,<>, chord, ? Off, On x, ^, <, <, -, >, 1ﾻ, ^;Glide Accent;\n"
"328;02h 48h;332;02h 4Ch;336;02h 50h;340;02h 54h;00h::7Fh;0::7 0, 1 0::7;*,, -, <, >,<>, chord, ? Off, On x, ^, <, <, -, >, 1ﾻ, ^;Glide Accent;\n"
"329;02h 49h;333;02h 4Dh;337;02h 51h;341;02h 55h;00h::7Fh;0::7 0, 1 0::7;*,, -, <, >,<>, chord, ? Off, On x, ^, <, <, -, >, 1ﾻ, ^;Glide Accent;\n"
"330;02h 4Ah;334;02h 4Eh;338;02h 52h;342;0 2h 5 6h;00h::7Fh;0::7 0, 1 0::7;*,, -, <, >,<>, chord, ? Off, On x, ^, <, <, -, >, 1ﾻ, ^;Glide Accent;\n"
"Arp Step Length / Timing;;;;;;;;;;;;\n"
"Step 1-4;;Step 5-8;;Step9-12;;Step 13-16;;;;;;\n"
"Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;sndv16;SNDV:o;Description;Name;\n"
"343;02h 57h;347;02h 5Bh;351;02h 5Fh;355;02h 63h;00h:7Fh;0::7 0::7;A, ^, <, <, -, >, ﾻ, ^ ?, ^, <, <, -, >, ﾻ, ^;Length Timing;\n"
"344;02h 58h;348;02h 5Ch;352;02h 60h;356;02h 64h;00h:7Fh;0::7 0::7;A, ^, <, <, -, >, ﾻ, ^ ?, ^, <, <, -, >, ﾻ, ^;Length Timing;\n"
"345;02h 59h;349;02h 5Dh;353;02h 61h;357;02h 65h;00h:7Fh;0::7 0::7;A, ^, <, <, -, >, ﾻ, ^ ?, ^, <, <, -, >, ﾻ, ^;Length Timing;\n"
"346;02h 5Ah;350;02h 5Eh;354;02h 62h;358;02h 66h;00h:7Fh;0::7 0::7;A, ^, <, <, -, >, ﾻ, ^ ?, ^, <, <, -, >, ﾻ, ^;Length Timing;\n"
"Sound Name;;;;;;;;;;;;\n"
"Char 1-4;;Char 5-8;;Char 9-12;;Char 13-16;;;;;;\n"
"Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;Idx;PAH PAL;sndv16;SNDV:o;Description;Name;\n"
"363;02h 6Bh;367;02h 6Fh;371;02h 73h;375;02h 77h;20h::7Fh;32::127;ASCII;Sound Name;\n"
"364;02h 6Ch;368;02h 70h;372;02h 74h;376;02h 78h;20h::7Fh;32::127;ASCII;Sound Name;\n"
"365;02h 6Dh;369;02h 71h;373;02h 75h;377;0 2h 7 9h;20h::7Fh;32::127;ASCII;Sound Name;\n"
"366;02h 6Eh;370;02h 72h;374;02h 76h;378;02h 7Ah;20h::7Fh;32::127;ASCII;Sound Name;\n"
";;;;;;;;;;;;\n"
"Idx;PAH PAL;;;;;;;sndv16;SNDV10;Description;Name;\n"
"379;02h 7Bh;;;;;;;20h::7Fh;32::127;ASCII;Sound Category;\n"
"380;02h 7Ch;;;;;;;20h::7Fh;32::127;ASCII;Sound Category;\n"
"381;02h 7Dh;;;;;;;20h::7Fh;32::127;ASCII;Sound Category;\n"
"382;02h 7Eh;;;;;;;20h::7Fh;32::127;ASCII;Sound Category;\n";

mqParameters::mqParameters()
{
	const auto lines = split(std::string(g_sdatCsv), '\n');

	std::vector<std::vector<std::string>> cells;
	cells.reserve(lines.size());

	for (const auto& line : lines)
		cells.push_back(split(line, ';'));

	auto cell = [&](const size_t _x, const size_t _y) -> std::string
	{
		if(_y >= cells.size())
			return {};
		const auto row = cells[_y];
		if(_x >= row.size())
			return {};
		return row[_x];
	};

	auto cellInt = [&](const size_t _x, const size_t _y, const int _default = 0) -> int
	{
		const auto s = cell(_x, _y);
		if(s.empty())
			return _default;
		return ::strtol(s.c_str(), nullptr, 10);
	};

	/* Relevant keys for us are:
	 * Idx = zero based parameter index
	 * PAH PAL = parameter index high and low
	 * Name = name
	 * sndv16 = parameter range
	 */

	for(size_t y=0; y<cells.size(); ++y)
	{
		const auto& row = cells[y];

		for(size_t x=0; x<row.size(); ++x)
		{
			if(cell(x,y) == "Idx" && cell(x+1,y) == "PAH PAL")
			{
				const auto namePrefix = cell(x, y-1);

				size_t idxName = 0;
				size_t idxSndv16 = 0;

				for(size_t i=x+2; i<row.size(); ++i)
				{
					if(cell(i,y) == "Name")
						idxName = i;
					else if(cell(i,y) == "sndv16")
						idxSndv16 = i;
				}

				if(!idxName || !idxSndv16)
					continue;

				for(size_t y2 = y+1; y2<cells.size(); ++y2)
				{
					const auto idx = cellInt(x,y2);
					auto pahpal = cell(x+1, y2);
					auto sndv16 = cell(idxSndv16, y2);

					// PAH PAL example: 12h 34h
					if(pahpal.size() != 7)
						break;

					auto name = cell(idxName, y2);

					if(name.empty())
					{
						auto i = cells[y2].size();

						while(i > 0)
						{
							--i;
							name = cell(i, y2);
							if(!name.empty())
								break;
						}
					}

					if(!namePrefix.empty())
						name = namePrefix + ' ' + name;

					LOG("Read Param " << idx << ", pahpal=" << pahpal << ", name=" << name << ", range=" << sndv16);

					pahpal[2] = 0;
					pahpal[6] = 0;

					const auto pah = strtol(pahpal.c_str(), nullptr, 16);
					const auto pal = strtol(pahpal.c_str() + 4, nullptr, 16);

					const auto pa = pah << 7 | pal;
					assert(pa == idx);
					if(pa != idx)
						break;

					assert(sndv16[2] == 'h');
					assert(sndv16.back() == 'h');

					sndv16[2] = 0;
					sndv16.back() = 0;

					Parameter p;

					p.pah = static_cast<uint8_t>(pah);
					p.pal = static_cast<uint8_t>(pal);

					p.valueMin = static_cast<uint8_t>(strtol(sndv16.c_str(), nullptr, 16));
					p.valueMax = static_cast<uint8_t>(strtol(sndv16.c_str() + sndv16.size() - 3, nullptr, 16));

					p.name = name;
					p.shortName = createShortName(p.name);

					m_parameters.push_back(p);
				}
			}
		}
	}
}

std::string mqParameters::getName(uint32_t _index) const
{
	if(_index >= m_parameters.size())
		return {};

	return m_parameters[_index].name;
}

std::string mqParameters::getShortName(uint32_t _index) const
{
	if(_index >= m_parameters.size())
		return {};

	return m_parameters[_index].shortName;
}


void replaceAll(std::string& _str, const std::string& _from, const std::string& _to)
{
	if (_from.empty())
		return;
	size_t pos = 0;
	while ((pos = _str.find(_from, pos)) != std::string::npos)
	{
		_str.replace(pos, _from.length(), _to);
		pos += _to.length();
	}
}

std::string mqParameters::createShortName(std::string name)
{
	// do the best we can to fit into a length of 8

	replaceAll(name, " ", "");
	replaceAll(name, "Volume", "Vol");
	replaceAll(name, "Envelope", "E");
	replaceAll(name, "Env", "E");
	replaceAll(name, "Osc", "O");
	replaceAll(name, "LFO", "L");
	replaceAll(name, "Filter", "F");
	replaceAll(name, "AmpE", "AE");
	replaceAll(name, "FiltE", "FE");
	replaceAll(name, "PhaserFX", "Phs");
	replaceAll(name, "ChorusFX", "Chr");
	replaceAll(name, "FlangerFX", "Flg");
	replaceAll(name, "VocoderFX", "Voc");
	replaceAll(name, "ReverbFX", "Rvb");
	replaceAll(name, "Step", "Stp");
	replaceAll(name, "Char", "Ch");
	replaceAll(name, "Noise", "N");
	replaceAll(name, "FiveFX", "5");
	replaceAll(name, "Attack", "Atk");
	replaceAll(name, "Release", "Rel");
	replaceAll(name, "Decay", "Dec");
	replaceAll(name, "Sustain", "Sus");
	replaceAll(name, "Overdrive", "Drv");

	return name;
}

std::vector<std::string> mqParameters::split(const std::string& _s, char _delim)
{
	std::stringstream ss(_s);
	std::string item;
	std::vector<std::string> elems;
	while (std::getline(ss, item, _delim))
		elems.push_back(std::move(item));
	return elems;
}
