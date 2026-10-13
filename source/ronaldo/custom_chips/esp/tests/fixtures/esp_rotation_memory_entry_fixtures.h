// Recorded IRAM/GRAM age witnesses; export_fixtures.py --rotation-memory-entry-audit.
#pragma once
#include "esp_hardware_fixtures.h"
namespace espRotationMemoryEntryTests {
using espHardwareTests::ProgramWord;
struct Fixture { const char* name; const ProgramWord* program; size_t count;
uint32_t before, expected; bool inputWitness; };
inline constexpr ProgramWord program0[] = {{0,0x00000c08},{4,0x00408000},{8,0x00148140},{12,0x003f8400},{16,0x00148540},{20,0x003f8000},{1072,0x00278540},{1076,0x00372c00},{1088,0x08000000},{1089,0x0a000000},{1090,0x08800000},{1091,0x02000000},{1092,0x00000000},{1112,0x00278140},{1116,0x00372c00},{1128,0x08000000},{1129,0x0a000000},{1130,0x09800000},{1131,0x02000000},{1132,0x00000000},{1224,0x00101380},{1225,0x00101380},{1226,0x00101380},{1227,0x00101380},{1228,0x00101380},{1229,0x00101380},{1230,0x00101380},{1231,0x00101380},{1236,0x00379400}};
// 2026-09-16-rotation-iram45/delay9_r0.u32 sha256=267537b7e9fde11031f14bda65f22d465e21fd277057a8383d1327f4942d8f5d
// 2026-09-16-rotation-iram45/delay12_r0.u32 sha256=b103dabb78fc1a59db888978f8a98ce2b31f2147193ab36aa2b245f3e6442262
// 2026-09-16-rotation-iram45/delay6_r0.u32 sha256=65c5ee5951fd70b4fa7b2df3c085f548848e403d22c3b0e1220ca22dda9fed92
// 2026-09-16-rotation-iram45/delay3_r0.u32 sha256=1591bc1325b085eb1ef43da7d43121d32b3d851fdc9169b5a9bf97f2cffe381a
// 2026-09-16-rotation-iram45/delay0_r0.u32 sha256=acc0202085103a06c5ef69c62e08a9fdf4e933f1ba2ee3cd70cf3b75a913960b
// 2026-09-16-rotation-iram45/delay21_r0.u32 sha256=ef4dbaeef25464057d31e90be269bde5c98fe93cfe23e8eb43214b25055f732a
// 2026-09-16-rotation-iram45/delay18_r0.u32 sha256=a2231fc4083a8dbcad9f2ec6c366a97558b2e6efe0c959e48c9a94f0cbd96fe2
inline constexpr ProgramWord program7[] = {{0,0x00000c08},{4,0x00408000},{8,0x00148140},{12,0x003c8000},{13,0x003f8400},{16,0x00248740},{20,0x003f8000},{1072,0x00278540},{1076,0x00372c00},{1088,0x08000000},{1089,0x0a000000},{1090,0x08800000},{1091,0x02000000},{1092,0x00000000},{1112,0x00278140},{1116,0x00372c00},{1128,0x08000000},{1129,0x0a000000},{1130,0x09800000},{1131,0x02000000},{1132,0x00000000},{1224,0x00101380},{1225,0x00101380},{1226,0x00101380},{1227,0x00101380},{1228,0x00101380},{1229,0x00101380},{1230,0x00101380},{1231,0x00101380},{1236,0x00379400}};
// 2026-09-16-rotation-gram46/delay9_r0.u32 sha256=4b2ce25c0d3314901498f50314212e011f5aed450734cc1ff773cfeb20bb4607
// 2026-09-16-rotation-gram46/delay12_r0.u32 sha256=b6558b6864ae5ad6f248303953f7bfb3b11addd7f48c3cfb9c1f20d4a96ca4c1
// 2026-09-16-rotation-gram46/delay6_r0.u32 sha256=3fa6bf735ee1c951504b9e0c786fc0b3f9ae240ea25ce2a47d8b9c0e351a2fb1
// 2026-09-16-rotation-gram46/delay3_r0.u32 sha256=b9d49aca1b99393ac10826ea3fe6966ad8f48f6262f89f5c4d4cc5592e5b91e2
// 2026-09-16-rotation-gram46/delay0_r0.u32 sha256=c4ed8c2256a1d95d8c139bb83714f4d7f6e2f57e4a14e1a9b6c6744e77da248b
// 2026-09-16-rotation-gram46/delay21_r0.u32 sha256=61a80f701af1178fc3a6f5682ae1ab0e45c516576647ad9c868bb7d397cd69b5
inline constexpr ProgramWord program13[] = {{0,0x00000c08},{4,0x00408000},{8,0x00148140},{12,0x003e0000},{13,0x003f8400},{16,0x00260740},{20,0x003f8000},{1072,0x00278540},{1076,0x00372c00},{1088,0x08000000},{1089,0x0a000000},{1090,0x08800000},{1091,0x02000000},{1092,0x00000000},{1112,0x00278140},{1116,0x00372c00},{1128,0x08000000},{1129,0x0a000000},{1130,0x09800000},{1131,0x02000000},{1132,0x00000000},{1224,0x00101380},{1225,0x00101380},{1226,0x00101380},{1227,0x00101380},{1228,0x00101380},{1229,0x00101380},{1230,0x00101380},{1231,0x00101380},{1236,0x00379400}};
// 2026-09-16-rotation-gram47/delay9_r0.u32 sha256=00d6c5e20f4398fda0c47f7da91a8d091b0d955d6fd2cd1cc95539760b6ed035
// 2026-09-16-rotation-gram47/delay12_r0.u32 sha256=0996fa54c98fa3fba7d974076c4e593758a4448e4e9b969d7259eb9d6fcd07e3
// 2026-09-16-rotation-gram47/delay3_r0.u32 sha256=aa7356ed8354d3df5e3f0745abba0393db4c061b6e4d41dac270b2f2a0b0a2f0
// 2026-09-16-rotation-gram47/delay0_r0.u32 sha256=1cf9b5bdcac216faf53dc793e9327c8fe2f36194e94730937e0df0650ca0b451
// 2026-09-16-rotation-gram47/delay21_r0.u32 sha256=ee2607f9eb2e9bda71102eb44e9da305a2b246d4e01327e7ee3d67c9f8f9e802
inline constexpr ProgramWord program18[] = {{0,0x00000c08},{4,0x00408000},{8,0x00148140},{12,0x003c4000},{13,0x003f8400},{16,0x00244740},{20,0x003f8000},{1072,0x00278540},{1076,0x00372c00},{1088,0x08000000},{1089,0x0a000000},{1090,0x08800000},{1091,0x02000000},{1092,0x00000000},{1112,0x00278140},{1116,0x00372c00},{1128,0x08000000},{1129,0x0a000000},{1130,0x09800000},{1131,0x02000000},{1132,0x00000000},{1224,0x00101380},{1225,0x00101380},{1226,0x00101380},{1227,0x00101380},{1228,0x00101380},{1229,0x00101380},{1230,0x00101380},{1231,0x00101380},{1236,0x00379400}};
// 2026-09-16-rotation-gram48/delay9_r0.u32 sha256=1620a64e6f9bd539bb1c93f92c25089f0e2ad88eabb1ec0e8a5eb48aeaf5aac3
// 2026-09-16-rotation-gram48/delay12_r0.u32 sha256=e12ca7932b8818153bc5ed615c6dbafd88adca019f5c7f6f71b2a5b066f71969
// 2026-09-16-rotation-gram48/delay6_r0.u32 sha256=f1d9f739d4c12738236afdd56501aaabe3a163ccbae1099ccaa736b9d0a41c5a
// 2026-09-16-rotation-gram48/delay3_r0.u32 sha256=9f3034c7ca4e9c54bf4871615ac4dcc40438dbe79b773e7a7cb2747e7cb2bc8a
// 2026-09-16-rotation-gram48/delay21_r0.u32 sha256=d9f767fa4b2f65aea39c5a4899132a4e888e4d41cfa82a2792f3056e856661c4
inline constexpr Fixture fixtures[] = {
{"2026-09-16-rotation-iram45/delay9_r0",program0,std::size(program0),0x1cc53,0x0f1000,false},
{"2026-09-16-rotation-iram45/delay12_r0",program0,std::size(program0),0x1c60a,0x001000,false},
{"2026-09-16-rotation-iram45/delay6_r0",program0,std::size(program0),0x1b27b,0x001000,false},
{"2026-09-16-rotation-iram45/delay3_r0",program0,std::size(program0),0x19ea6,0x0f1000,false},
{"2026-09-16-rotation-iram45/delay0_r0",program0,std::size(program0),0x19bbd,0x001000,false},
{"2026-09-16-rotation-iram45/delay21_r0",program0,std::size(program0),0x1997f,0x001000,false},
{"2026-09-16-rotation-iram45/delay18_r0",program0,std::size(program0),0x195b5,0x0f1000,false},
{"2026-09-16-rotation-gram46/delay9_r0",program7,std::size(program7),0x1caf6,0x030000,false},
{"2026-09-16-rotation-gram46/delay12_r0",program7,std::size(program7),0x1b575,0x030000,false},
{"2026-09-16-rotation-gram46/delay6_r0",program7,std::size(program7),0x1a1bd,0x001000,false},
{"2026-09-16-rotation-gram46/delay3_r0",program7,std::size(program7),0x18f18,0x001000,false},
{"2026-09-16-rotation-gram46/delay0_r0",program7,std::size(program7),0x18b66,0x030000,false},
{"2026-09-16-rotation-gram46/delay21_r0",program7,std::size(program7),0x187a1,0x030000,false},
{"2026-09-16-rotation-gram47/delay9_r0",program13,std::size(program13),0x1c2ee,0x001000,false},
{"2026-09-16-rotation-gram47/delay12_r0",program13,std::size(program13),0x1ad3e,0x001000,false},
{"2026-09-16-rotation-gram47/delay3_r0",program13,std::size(program13),0x1a5d5,0xffffc0,true},
{"2026-09-16-rotation-gram47/delay0_r0",program13,std::size(program13),0x192dd,0x001000,false},
{"2026-09-16-rotation-gram47/delay21_r0",program13,std::size(program13),0x18072,0xffffc0,true},
{"2026-09-16-rotation-gram48/delay9_r0",program18,std::size(program18),0x1c5ee,0x001000,false},
{"2026-09-16-rotation-gram48/delay12_r0",program18,std::size(program18),0x19f63,0x020000,false},
{"2026-09-16-rotation-gram48/delay6_r0",program18,std::size(program18),0x19bc5,0x020000,false},
{"2026-09-16-rotation-gram48/delay3_r0",program18,std::size(program18),0x198bf,0x001000,false},
{"2026-09-16-rotation-gram48/delay21_r0",program18,std::size(program18),0x183a5,0x020000,false},
};
}
