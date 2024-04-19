#include <cstdint>
#include <cstddef>

#include <array>

namespace wLib
{
	constexpr uint8_t g_fontTable0[] =
	{
		// CGRam Data, initially empty
		0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,	// 0 0
		0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,	// 0 1
		0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,	// 0 2
		0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,	// 0 3
		0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,	// 0 4
		0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,	// 0 5
		0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,	// 0 6
		0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,	// 0 7
		0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,	// 0 8
		0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,	// 0 9
		0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,	// 0 a
		0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,	// 0 b
		0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,	// 0 c
		0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,	// 0 d
		0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,	// 0 e
		0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,	// 0 f

		0b00000, // 1 0
		0b10001,
		0b01001,
		0b01110,
		0b10010,
		0b10001,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 1 1
		0b11110,
		0b00010,
		0b00010,
		0b00010,
		0b11111,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 1 2
		0b00110,
		0b00010,
		0b00110,
		0b01010,
		0b01010,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 1 3
		0b11111,
		0b00010,
		0b00010,
		0b00010,
		0b00010,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 1 4
		0b11111,
		0b00001,
		0b10001,
		0b10001,
		0b10001,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 1 5
		0b00110,
		0b00010,
		0b00010,
		0b00010,
		0b00010,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 1 6
		0b01110,
		0b00100,
		0b01000,
		0b00100,
		0b00010,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 1 7
		0b11111,
		0b10001,
		0b10001,
		0b10001,
		0b10001,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 1 8
		0b10111,
		0b10101,
		0b10101,
		0b10001,
		0b01111,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 1 9
		0b00110,
		0b00010,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 1 a
		0b01111,
		0b00001,
		0b00001,
		0b00001,
		0b00001,
		0b00001,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 1 b
		0b11110,
		0b00001,
		0b00001,
		0b00001,
		0b11110,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b10000, // 1 c
		0b11111,
		0b00001,
		0b00001,
		0b00001,
		0b00110,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 1 d
		0b11111,
		0b10001,
		0b10001,
		0b10001,
		0b11111,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 1 e
		0b10110,
		0b01001,
		0b10001,
		0b10001,
		0b10111,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 1 f
		0b00110,
		0b00010,
		0b00010,
		0b00010,
		0b00010,
		0b00010,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 2 0
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00100, // 2 1 !
		0b00100,
		0b00100,
		0b00100,
		0b00000,
		0b00000,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b01010, // 2 2 "
		0b01010,
		0b01010,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b01010, // 2 3 #
		0b01010,
		0b11111,
		0b01010,
		0b11111,
		0b01010,
		0b01010,
		0b00000,
		0b00000,
		0b00000,

		0b00100, // 2 4 $
		0b01111,
		0b10100,
		0b01110,
		0b00101,
		0b11110,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b11000, // 2 5 %
		0b11001,
		0b00010,
		0b00100,
		0b01000,
		0b10011,
		0b00011,
		0b00000,
		0b00000,
		0b00000,

		0b01100, // 2 6 &
		0b10010,
		0b10100,
		0b01000,
		0b10101,
		0b10010,
		0b01101,
		0b00000,
		0b00000,
		0b00000,

		0b01100, // 2 7 '
		0b00100,
		0b01000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00010, // 2 8 (
		0b00100,
		0b01000,
		0b01000,
		0b01000,
		0b00100,
		0b00010,
		0b00000,
		0b00000,
		0b00000,

		0b01000, // 2 9 )
		0b00100,
		0b00010,
		0b00010,
		0b00010,
		0b00100,
		0b01000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 2 a *
		0b00100,
		0b10101,
		0b01110,
		0b10101,
		0b00100,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 2 b +
		0b00100,
		0b00100,
		0b11111,
		0b00100,
		0b00100,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 2 c ,
		0b00000,
		0b00000,
		0b00000,
		0b01100,
		0b00100,
		0b01000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 2 d -
		0b00000,
		0b00000,
		0b11111,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 2 e .
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b01100,
		0b01100,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 2 f /
		0b00001,
		0b00010,
		0b00100,
		0b01000,
		0b10000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b01110, // 3 0 0
		0b10001,
		0b10011,
		0b10101,
		0b11001,
		0b10001,
		0b01110,
		0b00000,
		0b00000,
		0b00000,

		0b00100, // 3 1 1
		0b01100,
		0b00100,
		0b00100,
		0b00100,
		0b00100,
		0b01110,
		0b00000,
		0b00000,
		0b00000,

		0b01110, // 3 2 2
		0b10001,
		0b00001,
		0b00010,
		0b00100,
		0b01000,
		0b11111,
		0b00000,
		0b00000,
		0b00000,

		0b11111, // 3 3 3
		0b00010,
		0b00100,
		0b00010,
		0b00001,
		0b10001,
		0b01110,
		0b00000,
		0b00000,
		0b00000,

		0b00010, // 3 4 4
		0b00110,
		0b01010,
		0b10010,
		0b11111,
		0b00010,
		0b00010,
		0b00000,
		0b00000,
		0b00000,

		0b11111, // 3 5 5
		0b10000,
		0b11110,
		0b00001,
		0b00001,
		0b10001,
		0b01110,
		0b00000,
		0b00000,
		0b00000,

		0b00110, // 3 6 6
		0b01000,
		0b10000,
		0b11110,
		0b10001,
		0b10001,
		0b01110,
		0b00000,
		0b00000,
		0b00000,

		0b11111, // 3 7 7
		0b00001,
		0b00010,
		0b00100,
		0b01000,
		0b01000,
		0b01000,
		0b00000,
		0b00000,
		0b00000,

		0b01110, // 3 8 8
		0b10001,
		0b10001,
		0b01110,
		0b10001,
		0b10001,
		0b01110,
		0b00000,
		0b00000,
		0b00000,

		0b01110, // 3 9 9
		0b10001,
		0b10001,
		0b01111,
		0b00001,
		0b00010,
		0b01100,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 3 a :
		0b01100,
		0b01100,
		0b00000,
		0b01100,
		0b01100,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 3 b ;
		0b01100,
		0b01100,
		0b00000,
		0b01100,
		0b00100,
		0b01000,
		0b00000,
		0b00000,
		0b00000,

		0b00010, // 3 c <
		0b00100,
		0b01000,
		0b10000,
		0b01000,
		0b00100,
		0b00010,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 3 d =
		0b00000,
		0b11111,
		0b00000,
		0b11111,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b01000, // 3 e >
		0b00100,
		0b00010,
		0b00001,
		0b00010,
		0b00100,
		0b01000,
		0b00000,
		0b00000,
		0b00000,

		0b01110, // 3 f ?
		0b10001,
		0b00001,
		0b00010,
		0b00100,
		0b00000,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b01110, // 4 0 @
		0b10001,
		0b00001,
		0b01101,
		0b10101,
		0b10101,
		0b01110,
		0b00000,
		0b00000,
		0b00000,

		0b01110, // 4 1 A
		0b10001,
		0b10001,
		0b10001,
		0b11111,
		0b10001,
		0b10001,
		0b00000,
		0b00000,
		0b00000,

		0b11110, // 4 2 B
		0b10001,
		0b10001,
		0b11110,
		0b10001,
		0b10001,
		0b11110,
		0b00000,
		0b00000,
		0b00000,

		0b01110, // 4 3 C
		0b10001,
		0b10000,
		0b10000,
		0b10000,
		0b10001,
		0b01110,
		0b00000,
		0b00000,
		0b00000,

		0b11100, // 4 4 D
		0b10010,
		0b10001,
		0b10001,
		0b10001,
		0b10010,
		0b11100,
		0b00000,
		0b00000,
		0b00000,

		0b11111, // 4 5 E
		0b10000,
		0b10000,
		0b11110,
		0b10000,
		0b10000,
		0b11111,
		0b00000,
		0b00000,
		0b00000,

		0b11111, // 4 6 F
		0b10000,
		0b10000,
		0b11110,
		0b10000,
		0b10000,
		0b10000,
		0b00000,
		0b00000,
		0b00000,

		0b01110, // 4 7 G
		0b10001,
		0b10000,
		0b10111,
		0b10001,
		0b10001,
		0b01111,
		0b00000,
		0b00000,
		0b00000,

		0b10001, // 4 8 H
		0b10001,
		0b10001,
		0b11111,
		0b10001,
		0b10001,
		0b10001,
		0b00000,
		0b00000,
		0b00000,

		0b01110, // 4 9 I
		0b00100,
		0b00100,
		0b00100,
		0b00100,
		0b00100,
		0b11111,
		0b00000,
		0b00000,
		0b00000,

		0b00111, // 4 a J
		0b00010,
		0b00010,
		0b00010,
		0b00010,
		0b10010,
		0b01100,
		0b00000,
		0b00000,
		0b00000,

		0b10001, // 4 b K
		0b10010,
		0b10100,
		0b11000,
		0b10100,
		0b10010,
		0b10001,
		0b00000,
		0b00000,
		0b00000,

		0b10000, // 4 c L
		0b10000,
		0b10000,
		0b10000,
		0b10000,
		0b10000,
		0b11111,
		0b00000,
		0b00000,
		0b00000,

		0b10001, // 4 d M
		0b11011,
		0b10101,
		0b10101,
		0b10001,
		0b10001,
		0b10001,
		0b00000,
		0b00000,
		0b00000,

		0b10001, // 4 e N
		0b10001,
		0b11001,
		0b10101,
		0b10011,
		0b10001,
		0b10001,
		0b00000,
		0b00000,
		0b00000,

		0b01110, // 4 f O
		0b10001,
		0b10001,
		0b10001,
		0b10001,
		0b10001,
		0b01110,
		0b00000,
		0b00000,
		0b00000,

		0b11110, // 5 0 P
		0b10001,
		0b10001,
		0b11110,
		0b10000,
		0b10000,
		0b10000,
		0b00000,
		0b00000,
		0b00000,

		0b01110, // 5 1 Q
		0b10001,
		0b10001,
		0b10001,
		0b10101,
		0b10010,
		0b01101,
		0b00000,
		0b00000,
		0b00000,

		0b11110, // 5 2 R
		0b10001,
		0b10001,
		0b11110,
		0b10100,
		0b10010,
		0b10001,
		0b00000,
		0b00000,
		0b00000,

		0b01111, // 5 3 S
		0b10000,
		0b10000,
		0b01110,
		0b00001,
		0b00001,
		0b11110,
		0b00000,
		0b00000,
		0b00000,

		0b11111, // 5 4 T
		0b00100,
		0b00100,
		0b00100,
		0b00100,
		0b00100,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b10001, // 5 5 U
		0b10001,
		0b10001,
		0b10001,
		0b10001,
		0b10001,
		0b01110,
		0b00000,
		0b00000,
		0b00000,

		0b10001, // 5 6 V
		0b10001,
		0b10001,
		0b10001,
		0b10001,
		0b01010,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b10101, // 5 7 W
		0b10101,
		0b10101,
		0b10101,
		0b10101,
		0b10101,
		0b01010,
		0b00000,
		0b00000,
		0b00000,

		0b10001, // 5 8 X
		0b10001,
		0b01010,
		0b00100,
		0b01010,
		0b10001,
		0b10001,
		0b00000,
		0b00000,
		0b00000,

		0b10001, // 5 9 Y
		0b10001,
		0b10001,
		0b01010,
		0b00100,
		0b00100,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b11111, // 5 a Z
		0b00001,
		0b00010,
		0b00100,
		0b01000,
		0b10000,
		0b11111,
		0b00000,
		0b00000,
		0b00000,

		0b01110, // 5 b [
		0b01000,
		0b01000,
		0b01000,
		0b01000,
		0b01000,
		0b01110,
		0b00000,
		0b00000,
		0b00000,

		0b10001, // 5 c 
		0b01010,
		0b11111,
		0b00100,
		0b11111,
		0b00100,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b01110, // 5 d ]
		0b00010,
		0b00010,
		0b00010,
		0b00010,
		0b00010,
		0b01110,
		0b00000,
		0b00000,
		0b00000,

		0b00100, // 5 e ^
		0b01010,
		0b10001,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 5 f _
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b11111,
		0b00000,
		0b00000,
		0b00000,

		0b01000, // 6 0 `
		0b00100,
		0b00010,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 6 1 a
		0b00000,
		0b01110,
		0b00001,
		0b01111,
		0b10001,
		0b01111,
		0b00000,
		0b00000,
		0b00000,

		0b10000, // 6 2 b
		0b10000,
		0b10110,
		0b11001,
		0b10001,
		0b10001,
		0b11110,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 6 3 c
		0b00000,
		0b01110,
		0b10000,
		0b10000,
		0b10001,
		0b01110,
		0b00000,
		0b00000,
		0b00000,

		0b00001, // 6 4 d
		0b00001,
		0b01101,
		0b10011,
		0b10001,
		0b10001,
		0b01111,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 6 5 e
		0b00000,
		0b01110,
		0b10001,
		0b11111,
		0b10000,
		0b01110,
		0b00000,
		0b00000,
		0b00000,

		0b00110, // 6 6 f
		0b01001,
		0b01000,
		0b11100,
		0b01000,
		0b01000,
		0b01000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 6 7 g
		0b01111,
		0b10001,
		0b10001,
		0b01111,
		0b00001,
		0b01110,
		0b00000,
		0b00000,
		0b00000,

		0b10000, // 6 8 h
		0b10000,
		0b10110,
		0b11001,
		0b10001,
		0b10001,
		0b10001,
		0b00000,
		0b00000,
		0b00000,

		0b00100, // 6 9 i
		0b00000,
		0b01100,
		0b00100,
		0b00100,
		0b00100,
		0b01110,
		0b00000,
		0b00000,
		0b00000,

		0b00010, // 6 a j
		0b00000,
		0b00110,
		0b00010,
		0b00010,
		0b10010,
		0b01100,
		0b00000,
		0b00000,
		0b00000,

		0b10000, // 6 b k
		0b10000,
		0b10010,
		0b10100,
		0b11000,
		0b10100,
		0b10010,
		0b00000,
		0b00000,
		0b00000,

		0b01100, // 6 c l
		0b00100,
		0b00100,
		0b00100,
		0b00100,
		0b00100,
		0b01110,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 6 d m
		0b00000,
		0b11010,
		0b10101,
		0b10101,
		0b10001,
		0b10001,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 6 e n
		0b00000,
		0b10110,
		0b11001,
		0b10001,
		0b10001,
		0b10001,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 6 f o
		0b00000,
		0b01110,
		0b10001,
		0b10001,
		0b10001,
		0b01110,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 7 0 p
		0b00000,
		0b11110,
		0b10001,
		0b11110,
		0b10000,
		0b10000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 7 1 q
		0b00000,
		0b01101,
		0b10011,
		0b01111,
		0b00001,
		0b00001,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 7 2 r
		0b00000,
		0b10110,
		0b11001,
		0b10000,
		0b10000,
		0b10000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 7 3 s
		0b00000,
		0b01110,
		0b10000,
		0b01110,
		0b00001,
		0b11110,
		0b00000,
		0b00000,
		0b00000,

		0b01000, // 7 4 t
		0b01000,
		0b11100,
		0b01000,
		0b01000,
		0b01001,
		0b00110,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 7 5 u
		0b00000,
		0b10001,
		0b10001,
		0b10001,
		0b10011,
		0b01101,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 7 6 v
		0b00000,
		0b10001,
		0b10001,
		0b10001,
		0b01010,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 7 7 w
		0b00000,
		0b10001,
		0b10001,
		0b10001,
		0b10101,
		0b01010,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 7 8 x
		0b00000,
		0b10001,
		0b01010,
		0b00100,
		0b01010,
		0b10001,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 7 9 y
		0b00000,
		0b10001,
		0b10001,
		0b01111,
		0b00001,
		0b01110,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 7 a z
		0b00000,
		0b11111,
		0b00010,
		0b00100,
		0b01000,
		0b11111,
		0b00000,
		0b00000,
		0b00000,

		0b00010, // 7 b {
		0b00100,
		0b00100,
		0b01000,
		0b00100,
		0b00100,
		0b00010,
		0b00000,
		0b00000,
		0b00000,

		0b00100, // 7 c |
		0b00100,
		0b00100,
		0b00100,
		0b00100,
		0b00100,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b01000, // 7 d }
		0b00100,
		0b00100,
		0b00010,
		0b00100,
		0b00100,
		0b01000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 7 e ->
		0b00100,
		0b00010,
		0b11111,
		0b00010,
		0b00100,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 7 f <-
		0b00100,
		0b01000,
		0b11111,
		0b01000,
		0b00100,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 8 0 
		0b00110,
		0b00010,
		0b00010,
		0b00010,
		0b01110,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 8 1 
		0b11111,
		0b10001,
		0b10001,
		0b10001,
		0b01110,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 8 2 
		0b10001,
		0b10001,
		0b01001,
		0b00101,
		0b11110,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 8 3 
		0b01111,
		0b01001,
		0b01101,
		0b00001,
		0b00001,
		0b00001,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 8 4 
		0b11111,
		0b01001,
		0b01101,
		0b00001,
		0b11111,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 8 5 
		0b01001,
		0b00110,
		0b00010,
		0b00001,
		0b00001,
		0b00001,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 8 6 
		0b10001,
		0b01010,
		0b00100,
		0b00010,
		0b11111,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 8 7 
		0b11111,
		0b00001,
		0b10001,
		0b10110,
		0b10000,
		0b10000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 8 8 
		0b11111,
		0b00001,
		0b00001,
		0b00001,
		0b00001,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 8 9 
		0b10101,
		0b10101,
		0b10101,
		0b10101,
		0b01110,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 8 a 
		0b01111,
		0b01001,
		0b01001,
		0b01001,
		0b11001,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00100, // 8 b 
		0b01001,
		0b11101,
		0b00001,
		0b10001,
		0b11110,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 8 c 
		0b00010,
		0b00000,
		0b00010,
		0b00101,
		0b11110,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 8 d 
		0b00000,
		0b00000,
		0b00010,
		0b00101,
		0b11110,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00010, // 8 e 
		0b00100,
		0b01110,
		0b00000,
		0b00010,
		0b01100,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 8 f 
		0b00100,
		0b00000,
		0b00110,
		0b01000,
		0b11110,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00100, // 9 0 
		0b00101,
		0b11100,
		0b00000,
		0b00100,
		0b11111,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 9 1 
		0b00000,
		0b00100,
		0b01010,
		0b00001,
		0b11111,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 9 2 
		0b00010,
		0b00000,
		0b10011,
		0b10011,
		0b11111,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 9 3 
		0b00000,
		0b00000,
		0b00111,
		0b00101,
		0b11011,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 9 4 
		0b01100,
		0b00010,
		0b01001,
		0b10101,
		0b11111,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00100, // 9 5 
		0b00000,
		0b01111,
		0b01001,
		0b00110,
		0b11001,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00100, // 9 6 
		0b01001,
		0b11101,
		0b00001,
		0b00001,
		0b00001,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 9 7 
		0b00101,
		0b00000,
		0b00010,
		0b00101,
		0b11110,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 9 8 
		0b00000,
		0b00000,
		0b01100,
		0b01010,
		0b01101,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 9 9 
		0b01010,
		0b00000,
		0b01100,
		0b01010,
		0b01101,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 9 a 
		0b00000,
		0b01111,
		0b01001,
		0b00110,
		0b11001,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00100, // 9 b 
		0b00000,
		0b00100,
		0b01010,
		0b00001,
		0b11111,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 9 c 
		0b00101,
		0b00000,
		0b10011,
		0b10011,
		0b11111,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 9 d 
		0b00001,
		0b00001,
		0b00001,
		0b00001,
		0b11110,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // 9 e 
		0b00000,
		0b00000,
		0b00100,
		0b01010,
		0b01110,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00110, // 9 f 
		0b11101,
		0b00001,
		0b00001,
		0b00001,
		0b00001,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // a 0 
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // a 1 
		0b00000,
		0b00000,
		0b00000,
		0b11100,
		0b10100,
		0b11100,
		0b00000,
		0b00000,
		0b00000,

		0b00111, // a 2 
		0b00100,
		0b00100,
		0b00100,
		0b00100,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // a 3 
		0b00000,
		0b00000,
		0b00100,
		0b00100,
		0b00100,
		0b11100,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // a 4 
		0b00000,
		0b00000,
		0b00000,
		0b10000,
		0b01000,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // a 5 
		0b00000,
		0b00000,
		0b01100,
		0b01100,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // a 6 
		0b11111,
		0b00001,
		0b11111,
		0b00001,
		0b00010,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // a 7 
		0b00000,
		0b11111,
		0b00001,
		0b00110,
		0b00100,
		0b01000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // a 8 
		0b00000,
		0b00010,
		0b00100,
		0b01100,
		0b10100,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // a 9 
		0b00000,
		0b00100,
		0b11111,
		0b10001,
		0b00001,
		0b00110,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // a a 
		0b00000,
		0b00000,
		0b11111,
		0b00100,
		0b00100,
		0b11111,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // a b 
		0b00000,
		0b00010,
		0b11111,
		0b00110,
		0b01010,
		0b10010,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // a c 
		0b00000,
		0b01000,
		0b11111,
		0b01001,
		0b01010,
		0b01000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // a d 
		0b00000,
		0b00000,
		0b01110,
		0b00010,
		0b00010,
		0b11111,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // a e 
		0b00000,
		0b00000,
		0b11110,
		0b00010,
		0b11110,
		0b00010,
		0b11110,
		0b00000,
		0b00000,

		0b00000, // a f 
		0b00000,
		0b00000,
		0b10101,
		0b10101,
		0b00001,
		0b00110,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // b 0 
		0b00000,
		0b00000,
		0b11111,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b11111, // b 1 
		0b00001,
		0b00101,
		0b00110,
		0b00100,
		0b00100,
		0b01000,
		0b00000,
		0b00000,
		0b00000,

		0b00001, // b 2 
		0b00010,
		0b00100,
		0b01100,
		0b10100,
		0b00100,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b00100, // b 3 
		0b11111,
		0b10001,
		0b10001,
		0b00001,
		0b00010,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // b 4 
		0b11111,
		0b00100,
		0b00100,
		0b00100,
		0b00100,
		0b11111,
		0b00000,
		0b00000,
		0b00000,

		0b00010, // b 5 
		0b11111,
		0b00010,
		0b00110,
		0b01010,
		0b10010,
		0b00010,
		0b00000,
		0b00000,
		0b00000,

		0b01000, // b 6 
		0b11111,
		0b01001,
		0b01001,
		0b01001,
		0b01001,
		0b10010,
		0b00000,
		0b00000,
		0b00000,

		0b00100, // b 7 
		0b11111,
		0b00100,
		0b11111,
		0b00100,
		0b00100,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // b 8 
		0b01111,
		0b01001,
		0b10001,
		0b00001,
		0b00010,
		0b01100,
		0b00000,
		0b00000,
		0b00000,

		0b01000, // b 9 
		0b01111,
		0b10010,
		0b00010,
		0b00010,
		0b00010,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // b a 
		0b11111,
		0b00001,
		0b00001,
		0b00001,
		0b00001,
		0b11111,
		0b00000,
		0b00000,
		0b00000,

		0b01010, // b b 
		0b11111,
		0b01010,
		0b01010,
		0b00010,
		0b00100,
		0b01000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // b c 
		0b11000,
		0b00001,
		0b11001,
		0b00001,
		0b00010,
		0b11100,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // b d 
		0b11111,
		0b00001,
		0b00010,
		0b00100,
		0b01010,
		0b10001,
		0b00000,
		0b00000,
		0b00000,

		0b01000, // b e 
		0b11111,
		0b01001,
		0b01010,
		0b01000,
		0b01000,
		0b00111,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // b f 
		0b10001,
		0b10001,
		0b01001,
		0b00001,
		0b00010,
		0b01100,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // c 0 
		0b01111,
		0b01001,
		0b10101,
		0b00011,
		0b00010,
		0b01100,
		0b00000,
		0b00000,
		0b00000,

		0b00010, // c 1 
		0b11100,
		0b00100,
		0b11111,
		0b00100,
		0b00100,
		0b01000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // c 2 
		0b10101,
		0b10101,
		0b10101,
		0b00001,
		0b00010,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b01110, // c 3 
		0b00000,
		0b11111,
		0b00100,
		0b00100,
		0b00100,
		0b01000,
		0b00000,
		0b00000,
		0b00000,

		0b01000, // c 4 
		0b01000,
		0b01000,
		0b01100,
		0b01010,
		0b01000,
		0b01000,
		0b00000,
		0b00000,
		0b00000,

		0b00100, // c 5 
		0b00100,
		0b11111,
		0b00100,
		0b00100,
		0b01000,
		0b10000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // c 6 
		0b01110,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b11111,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // c 7 
		0b11111,
		0b00001,
		0b01010,
		0b00100,
		0b01010,
		0b10000,
		0b00000,
		0b00000,
		0b00000,

		0b00100, // c 8 
		0b11111,
		0b00010,
		0b00100,
		0b01110,
		0b10101,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b00010, // c 9 
		0b00010,
		0b00010,
		0b00010,
		0b00010,
		0b00100,
		0b01000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // c a 
		0b00100,
		0b00010,
		0b10001,
		0b10001,
		0b10001,
		0b10001,
		0b00000,
		0b00000,
		0b00000,

		0b10000, // c b 
		0b10000,
		0b11111,
		0b10000,
		0b10000,
		0b10000,
		0b01111,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // c c 
		0b11111,
		0b00001,
		0b00001,
		0b00001,
		0b00010,
		0b01100,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // c d 
		0b01000,
		0b10100,
		0b00010,
		0b00001,
		0b00001,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00100, // c e 
		0b11111,
		0b00100,
		0b10101,
		0b10101,
		0b00100,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // c f 
		0b11111,
		0b00001,
		0b00001,
		0b01010,
		0b00100,
		0b00010,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // d 0 
		0b01110,
		0b00000,
		0b01110,
		0b00000,
		0b01110,
		0b00001,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // d 1 
		0b00100,
		0b01000,
		0b10000,
		0b10001,
		0b11111,
		0b00001,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // d 2 
		0b00001,
		0b00001,
		0b01010,
		0b00100,
		0b01010,
		0b10000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // d 3 
		0b11111,
		0b01000,
		0b11111,
		0b01000,
		0b01000,
		0b00111,
		0b00000,
		0b00000,
		0b00000,

		0b01000, // d 4 
		0b01000,
		0b11111,
		0b01001,
		0b01010,
		0b01000,
		0b01000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // d 5 
		0b01110,
		0b00010,
		0b00010,
		0b00010,
		0b00010,
		0b11111,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // d 6 
		0b11111,
		0b00001,
		0b11111,
		0b00001,
		0b00001,
		0b11111,
		0b00000,
		0b00000,
		0b00000,

		0b01110, // d 7 
		0b00000,
		0b11111,
		0b00001,
		0b00001,
		0b00010,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b10010, // d 8 
		0b10010,
		0b10010,
		0b10010,
		0b00010,
		0b00100,
		0b01000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // d 9 
		0b00100,
		0b10100,
		0b10100,
		0b10100,
		0b10101,
		0b10110,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // d a 
		0b10000,
		0b10000,
		0b10001,
		0b10010,
		0b10100,
		0b11000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // d b 
		0b11111,
		0b10001,
		0b10001,
		0b10001,
		0b10001,
		0b11111,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // d c 
		0b11111,
		0b10001,
		0b10001,
		0b00001,
		0b00010,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // d d 
		0b11000,
		0b00000,
		0b00001,
		0b00001,
		0b00010,
		0b11100,
		0b00000,
		0b00000,
		0b00000,

		0b00100, // d e 
		0b10010,
		0b01000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b11100, // d f 
		0b10100,
		0b11100,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // e 0 
		0b01001,
		0b10101,
		0b10010,
		0b10010,
		0b01101,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b01010, // e 1 
		0b00000,
		0b01110,
		0b00001,
		0b01111,
		0b10001,
		0b01111,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // e 2 
		0b00000,
		0b01110,
		0b10001,
		0b11110,
		0b10001,
		0b11110,
		0b10000,
		0b10000,
		0b10000,

		0b00000, // e 3 
		0b00000,
		0b01110,
		0b10000,
		0b01100,
		0b10001,
		0b01110,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // e 4 
		0b00000,
		0b10001,
		0b10001,
		0b10001,
		0b10011,
		0b11101,
		0b10000,
		0b10000,
		0b10000,

		0b00000, // e 5 
		0b00000,
		0b00000,
		0b01111,
		0b10100,
		0b10010,
		0b10001,
		0b01110,
		0b00000,
		0b00000,

		0b00000, // e 6 
		0b00000,
		0b00110,
		0b01001,
		0b10001,
		0b10001,
		0b11110,
		0b10000,
		0b10000,
		0b10000,

		0b00000, // e 7 
		0b00000,
		0b01111,
		0b10001,
		0b10001,
		0b10001,
		0b01111,
		0b00001,
		0b00001,
		0b01110,

		0b00000, // e 8 
		0b00000,
		0b00111,
		0b00100,
		0b00100,
		0b10100,
		0b01000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // e 9 
		0b00010,
		0b11010,
		0b00010,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00010, // e a 
		0b00000,
		0b00110,
		0b00010,
		0b00010,
		0b00010,
		0b00010,
		0b00010,
		0b10010,
		0b01100,

		0b00000, // e b 
		0b10100,
		0b01000,
		0b10100,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // e c 
		0b00100,
		0b01110,
		0b10100,
		0b10101,
		0b01110,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b01000, // e d 
		0b01000,
		0b11100,
		0b01000,
		0b11100,
		0b01000,
		0b01111,
		0b00000,
		0b00000,
		0b00000,

		0b01110, // e e 
		0b00000,
		0b10110,
		0b11001,
		0b10001,
		0b10001,
		0b10001,
		0b00000,
		0b00000,
		0b00000,

		0b01010, // e f 
		0b00000,
		0b01110,
		0b10001,
		0b10001,
		0b10001,
		0b01110,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // f 0 
		0b00000,
		0b10110,
		0b11001,
		0b10001,
		0b10001,
		0b11110,
		0b10000,
		0b10000,
		0b10000,

		0b00000, // f 1 
		0b00000,
		0b01101,
		0b10011,
		0b10001,
		0b10001,
		0b01111,
		0b00001,
		0b00001,
		0b00001,

		0b00000, // f 2 
		0b01110,
		0b10001,
		0b11111,
		0b10001,
		0b10001,
		0b01110,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // f 3 
		0b00000,
		0b00000,
		0b01011,
		0b10101,
		0b11010,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // f 4 
		0b00000,
		0b01110,
		0b10001,
		0b10001,
		0b01010,
		0b11011,
		0b00000,
		0b00000,
		0b00000,

		0b01010, // f 5 
		0b00000,
		0b10001,
		0b10001,
		0b10001,
		0b10011,
		0b01101,
		0b00000,
		0b00000,
		0b00000,

		0b11111, // f 6 
		0b10000,
		0b01000,
		0b00100,
		0b01000,
		0b10000,
		0b11111,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // f 7 
		0b00000,
		0b11111,
		0b01010,
		0b01010,
		0b01010,
		0b10011,
		0b00000,
		0b00000,
		0b00000,

		0b11111, // f 8 
		0b00000,
		0b10001,
		0b01010,
		0b00100,
		0b01010,
		0b10001,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // f 9 
		0b00000,
		0b10001,
		0b10001,
		0b10001,
		0b10001,
		0b01111,
		0b00001,
		0b00001,
		0b01110,

		0b00000, // f a 
		0b00001,
		0b11110,
		0b00100,
		0b11111,
		0b00100,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // f b 
		0b00000,
		0b11111,
		0b01000,
		0b01111,
		0b01001,
		0b10001,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // f c 
		0b00000,
		0b11111,
		0b10101,
		0b11111,
		0b10001,
		0b10001,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // f d 
		0b00000,
		0b00100,
		0b00000,
		0b11111,
		0b00000,
		0b00100,
		0b00000,
		0b00000,
		0b00000,

		0b00000, // f e 
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,

		0b11111, // f f 
		0b11111,
		0b11111,
		0b11111,
		0b11111,
		0b11111,
		0b11111,
		0b11111,
		0b11111,
		0b11111,
	};

	static_assert(std::size(g_fontTable0) == static_cast<size_t>(256 * 10));

	const uint8_t* getCharacterData(const uint8_t _character)
	{
		return &g_fontTable0[static_cast<size_t>(_character) * 10];
	}
}