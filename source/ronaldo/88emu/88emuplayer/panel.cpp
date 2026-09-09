/*
 * Derived from Nuked-SC55, which carries:
 *
 * Copyright (C) 2021, 2024 nukeykt
 *
 * This file is part of Nuked-SC55.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "panel.hpp"

#include <algorithm>
#include <cstddef>

namespace sc88panel
{
	namespace
	{
		inline void putPixel(uint32_t* _dst, const int32_t _row, const int32_t _col, const uint32_t _c)
		{
			if(_row < 0 || _row >= kHeight || _col < 0 || _col >= kWidth)
				return;
			_dst[static_cast<size_t>(_row) * kWidth + static_cast<size_t>(_col)] = _c;
		}

		const uint8_t* glyph(const uint8_t _ch, const uint8_t* _cgRam)
		{
			return _ch >= 16 ? &lcd_font[_ch - 16][0] : &_cgRam[(_ch & 7) * 8];
		}

	// 5x7 character cell, 6-pixel pitch, each dot drawn as a 5x5 block —
	// matching the reference's LCD_FontRenderStandard.
	void drawChar(uint32_t* _dst, const int32_t _row, const int32_t _col,
	                        const uint8_t _ch, const uint8_t* _cgRam,
	                        const uint32_t _on, const uint32_t _off)
	{
		const uint8_t* f = glyph(_ch, _cgRam);

		for(int i = 0; i < 7; ++i)
		{
			for(int j = 0; j < 5; ++j)
			{
				const uint32_t col = (f[i] & (1u << (4 - j))) ? _on : _off;
				const int32_t r = _row + i * 6;
				const int32_t c = _col + j * 6;
				for(int ii = 0; ii < 5; ++ii)
					for(int jj = 0; jj < 5; ++jj)
						putPixel(_dst, r + ii, c + jj, col);
			}
		}
	}

	// Level-meter segment: 8 rows tall, `_width` columns of big 9x24 blocks.
	void drawLevel(uint32_t* _dst, const int32_t _row, const int32_t _col,
	                         const uint8_t _ch, const uint8_t* _cgRam, const int _width,
	                         const uint32_t _on, const uint32_t _off)
	{
		const uint8_t* f = glyph(_ch, _cgRam);

		for(int i = 0; i < 8; ++i)
		{
			for(int j = 0; j < _width; ++j)
			{
				const uint32_t col = (f[i] & (1u << (4 - j))) ? _on : _off;
				const int32_t r = _row + i * 11;
				const int32_t c = _col + j * 26;
				for(int ii = 0; ii < 9; ++ii)
					for(int jj = 0; jj < 24; ++jj)
						putPixel(_dst, r + ii, c + jj, col);
			}
		}
	}

	void drawLr(uint32_t* _dst, const uint8_t _ch, const uint8_t* _cgRam,
	                      const uint32_t _on, const uint32_t _off)
	{
		const uint8_t* f = glyph(_ch, _cgRam);
		const uint32_t col = (f[0] & 1) ? _on : _off;

		for(int g = 0; g < 2; ++g)
		{
			for(int i = 0; i < 12; ++i)
			{
				for(int j = 0; j < 11; ++j)
				{
					if(kLrGlyph[g][i][j])
						putPixel(_dst, i + kLrPos[g][0], j + kLrPos[g][1], col);
				}
			}
		}
	}

	void drawContents(uint32_t* _dst, const uint8_t* _ddRam, const uint8_t* _cgRam,
	                            const uint32_t _on, const uint32_t _off)
	{
		for(const auto& tf : kTextFields)
		{
			for(int i = 0; i < tf.count; ++i)
				drawChar(_dst, tf.row, tf.column + i * 35, _ddRam[tf.ddRamStart + i], _cgRam, _on, _off);
		}

		drawLr(_dst, _ddRam[kLrDdRam], _cgRam, _on, _off);

		// Two 4-segment level meters. The last segment of each is 1 dot wide
		// (it is the meter's peak marker), the rest 5.
		for(int ch = 0; ch < 2; ++ch)
		{
			for(int seg = 0; seg < 4; ++seg)
			{
				drawLevel(_dst, 71 + ch * 88, 293 + seg * 130,
				          _ddRam[kLevelDdRam[ch] + seg], _cgRam, seg == 3 ? 1 : 5, _on, _off);
			}
		}
	}

	} // namespace

	void renderOverlay(uint32_t* _dst, const uint8_t* _ddRam, const uint8_t* _cgRam,
	                             const bool _blank, const uint32_t _on, const uint32_t _off)
	{
		std::fill(_dst, _dst + static_cast<size_t>(kWidth) * kHeight, uint32_t{0});
		if(!_blank)
			drawContents(_dst, _ddRam, _cgRam, _on, _off);
	}
}
