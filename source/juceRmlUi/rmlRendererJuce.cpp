#include "rmlRendererJuce.h"

#include <algorithm>
#include <cassert>

#include "juce_graphics/juce_graphics.h"

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#	define HAVE_SSE
#endif

namespace juceRmlUi
{
	namespace
	{
		enum class HostEndian
		{
			Big,
			Little
		};

		constexpr HostEndian hostEndian()
		{
			constexpr uint32_t test32 = 0x01020304;
			constexpr uint8_t test8 = static_cast<const uint8_t&>(test32);

			static_assert(test8 == 0x01 || test8 == 0x04, "unable to determine endianess");

			return test8 == 0x01 ? HostEndian::Big : HostEndian::Little;
		}
	}
	namespace rendererJuce
	{
		struct Triangle
		{
			Rml::Vector2f p0;
			Rml::Vector2f p1;
			Rml::Vector2f p2;
		};

		struct Quad
		{
			Rml::Rectanglef position;
			Rml::Rectanglef uv;
			Rml::Vector2f uvPerPixel;
			Rml::ColourbPremultiplied color;
			bool hasColor;
		};

		struct Geometry
		{
			Rml::Rectanglef bounds;
			Rml::Rectanglef uvBounds;
			std::vector<Quad> quads;
		};

		template<typename T>
		struct Color
		{
			T r,g,b,a;

			Color operator + (const Color& _c) const noexcept { return Color{ T(r + _c.r), T(g + _c.g), T(b + _c.b), T(a + _c.a) }; }
			Color operator - (const Color& _c) const noexcept { return Color{ T(r - _c.r), T(g - _c.g), T(b - _c.b), T(a - _c.a) }; }

			Color& operator += (const Color& _c) noexcept { r += _c.r; g += _c.g; b += _c.b; a += _c.a; return *this; }
			Color& operator -= (const Color& _c) noexcept { r -= _c.r; g -= _c.g; b -= _c.b; a -= _c.a; return *this; }
			Color& operator *= (const Color& _v) noexcept
			{
				*this = *this * _v;
				return *this;
			}

			template<typename U = T> std::enable_if_t<std::is_same_v<U, uint8_t>, Color<uint8_t>>
			operator * (const Color<uint8_t>& _c) const noexcept
			{
				return Color<uint8_t>
				{
					static_cast<uint8_t>((static_cast<uint16_t>(r) * _c.r) >> 8),
					static_cast<uint8_t>((static_cast<uint16_t>(g) * _c.g) >> 8),
					static_cast<uint8_t>((static_cast<uint16_t>(b) * _c.b) >> 8),
					static_cast<uint8_t>((static_cast<uint16_t>(a) * _c.a) >> 8)
				};
			}

			template<typename U = T> std::enable_if_t<std::is_same_v<U, uint8_t>, Color<uint8_t>>
			operator * (const uint8_t _c) const noexcept
			{
				return Color<uint8_t>
				{
					static_cast<uint8_t>((static_cast<uint16_t>(r) * _c) >> 8),
					static_cast<uint8_t>((static_cast<uint16_t>(g) * _c) >> 8),
					static_cast<uint8_t>((static_cast<uint16_t>(b) * _c) >> 8),
					static_cast<uint8_t>((static_cast<uint16_t>(a) * _c) >> 8)
				};
			}

			template<typename U = T> std::enable_if_t<std::is_same_v<U, float>, Color<float>>
			operator * (const float _v) const noexcept
			{
				return Color<float>{r * _v, g * _v, b * _v, a * _v};
			}

			template<typename U = T> std::enable_if_t<std::is_same_v<U, int>, Color<int>>
			operator * (const int _v) const noexcept
			{
				return Color<int>{r * _v, g * _v, b * _v, a * _v};
			}

			template<typename U = T> std::enable_if_t<std::is_same_v<U, int>, Color<int>>
			operator * (const Color<int>& _c) const noexcept
			{
				return Color<int>{r * _c.r, g * _c.g, b * _c.b, a * _c.a};
			}

			template<typename U = T> std::enable_if_t<std::is_same_v<U, int>, Color<int>>
			operator >> (const int _v) const noexcept
			{
				return Color<int>{r >> _v, g >> _v, b >> _v, a >> _v};
			}
		};

		using Colorb = Color<uint8_t>;
		using Colori = Color<int>;
		using Colorf = Color<float>;
		using Colors = Color<int16_t>;

		float toFloat(uint8_t _v) noexcept { return _v; }
		int toInt(uint8_t _v) noexcept { return _v; }
		int16_t toShort(uint8_t _v) noexcept { return _v; }

		uint8_t toByte(float _v) noexcept { return static_cast<uint8_t>(_v); }
		uint8_t toByte(int _v) noexcept { return static_cast<uint8_t>(_v); }

		Colorf toFloat(const Colorb& _c) noexcept { return Colorf{ toFloat(_c.r), toFloat(_c.g), toFloat(_c.b), toFloat(_c.a)}; }
		Colori toInt(const Colorb& _c) noexcept { return Colori{ toInt(_c.r), toInt(_c.g), toInt(_c.b), toInt(_c.a)}; }
		Colors toShort(const Colorb& _c) noexcept { return Colors{ toShort(_c.r), toShort(_c.g), toShort(_c.b), toShort(_c.a)}; }
		Colorb toByte(const Colorf& _c) noexcept { return Colorb{ toByte(_c.r), toByte(_c.g), toByte(_c.b), toByte(_c.a) }; }
		Colorb toByte(const Colori& _c) noexcept { return Colorb{ toByte(_c.r), toByte(_c.g), toByte(_c.b), toByte(_c.a) }; }

		struct Image
		{
			static constexpr int PadW = 4;	// minimum 1 and up to N bytes of padding to ensure we have pixel groups of width N, required for some SIMD ops
			static constexpr int PadH = 1;

			int width = 0;
			int paddedWidth = 0;
			int height = 0;
			bool hasAlpha = false;

			std::vector<uint8_t> data;	// RGBA

			uint8_t* getBytePointer(const size_t _x, const size_t _y) noexcept
			{
				return data.data() + ((getIndex(_x, _y)) << 2);
			}

			Colorb* getColorPointer(const size_t _x, const size_t _y) noexcept
			{
				return reinterpret_cast<Colorb*>(data.data()) + getIndex(_x, _y);
			}

			const Colorb* getColorPointer(const size_t _x, const size_t _y) const noexcept
			{
				return reinterpret_cast<const Colorb*>(data.data()) + getIndex(_x, _y);
			}

			static constexpr int padWidth(int _width) noexcept
			{
				_width += 1;
				_width += (PadW - 1);
				_width -= (_width & (PadW-1));
				return _width;
			}

			static constexpr int padHeight(const int _height) noexcept
			{
				return _height + 1;
			}

		private:
			size_t getIndex(const size_t _x, const size_t _y) const
			{
				return _y * paddedWidth + _x;
			}
		};

		static_assert(Image::padWidth(7) == 8);
		static_assert(Image::padWidth(8) == 12);
		static_assert(Image::padWidth(9) == 12);
		static_assert(Image::padWidth(10) == 12);
		static_assert(Image::padWidth(11) == 12);
		static_assert(Image::padWidth(12) == 16);

		template<bool HasAlphaBlend, bool HasColor>
		static void blit(Image& _dst, const int _dstX, const int _dstY, const Image& _src, const int _srcX, const int _srcY, const int _srcW, const int _srcH, const Colorb& _color)
		{
			const auto* src = _src.getColorPointer(_srcX, _srcY);

			auto* dst = _dst.getColorPointer(_dstX, _dstY);

			for (int y = 0; y < _srcH; ++y)
			{
				if constexpr (HasColor || HasAlphaBlend)
				{
					src = _src.getColorPointer(_srcX, _srcY + y);
					dst = _dst.getColorPointer(_dstX, _dstY + y);

					for (int x = 0; x < _srcW; ++x, ++src, ++dst)
					{
						if constexpr (HasAlphaBlend)
						{
							auto existing = toInt(*dst);
							const auto srcCol = toInt(*src * _color);
							const auto invAlpha = 255 - srcCol.a;
							auto newDst = srcCol + ((existing * invAlpha) >> 8);
							*dst = toByte(newDst);
						}
						else
						{
							*dst = *src * _color;
						}
					}
				}
				else
				{
					memcpy(dst, src, _srcW * sizeof(Colorb));
					src += _src.paddedWidth;
					dst += _dst.paddedWidth;
				}
			}
		}

		template<bool AlphaBlend, bool Color>
		static void blit(
			Image& _dst, const Image& _src,
			const int _srcX, const int _srcY, const int _srcW, const int _srcH,
			const int _dstX, const int _dstY, const int _dstW, const int _dstH, const Colorb& _color)
		{
			static constexpr int scaleBits = 18;	// use 14.18 fixed point for the filtering code

			int srcY = _srcY << scaleBits;

			const int srcXStep = (_srcW << scaleBits) / _dstW;
			const int srcYStep = (_srcH << scaleBits) / _dstH;

			auto col = toInt(_color);
			++col.a;	// adjust for multiplication with right shift later to ensure result is opaque for max alpha, i.e. 255 * (255+1) >> 8 = 255

			auto col16 = toShort(_color);

			for (int y=0; y<_dstH; ++y)
			{
				const int srcYi = srcY >> scaleBits;
				const int fracY = srcY - (srcYi << scaleBits);

				int srcX = _srcX << scaleBits;

				auto* dst = _dst.getColorPointer(_dstX, _dstY + y);

				int x=0;

#ifdef HAVE_SSE
				for (; x < _dstW - 2; x += 2)
				{
					// bilinear filtering executed for 2 pixels in parallel, doing 2x2 texel fetches i.e. 8 texels per iteration
					const int srcXiA = srcX >> scaleBits;
					const int fracXA = srcX - (srcXiA << scaleBits);
					srcX += srcXStep;

					const int srcXiB = srcX >> scaleBits;
					const int fracXB = srcX - (srcXiB << scaleBits);
					srcX += srcXStep;

					// fetch 8 texels, load 2 at a time
					const auto* c00ptrA = _src.getColorPointer(srcXiA, srcYi);
					const auto* c01ptrA = c00ptrA + _src.paddedWidth;

					const auto* c00ptrB = _src.getColorPointer(srcXiB, srcYi);
					const auto* c01ptrB = c00ptrB + _src.paddedWidth;

					__m128i c0010A = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(c00ptrA));
					__m128i c0111A = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(c01ptrA));

					__m128i c0010B = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(c00ptrB));
					__m128i c0111B = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(c01ptrB));

					// pack into single registers
					__m128i c0010 = _mm_slli_si128(c0010B, 8);
					c0010 = _mm_or_si128(c0010, c0010A);

					__m128i c0111 = _mm_slli_si128(c0111B, 8);
					c0111 = _mm_or_si128(c0111, c0111A);

					// unpack into 16-bit integers, i.e. 4x RGBA8 -> 4x RGBA16
					__m128i c00 = _mm_cvtepu8_epi16(_mm_shuffle_epi32(c0010, _MM_SHUFFLE(2, 0, 2, 0)));
					__m128i c10 = _mm_cvtepu8_epi16(_mm_shuffle_epi32(c0010, _MM_SHUFFLE(3, 1, 3, 1)));
					__m128i c01 = _mm_cvtepu8_epi16(_mm_shuffle_epi32(c0111, _MM_SHUFFLE(2, 0, 2, 0)));
					__m128i c11 = _mm_cvtepu8_epi16(_mm_shuffle_epi32(c0111, _MM_SHUFFLE(3, 1, 3, 1)));

					// calc row differences
					__m128i d0 = _mm_sub_epi16(c10, c00);
					__m128i d1 = _mm_sub_epi16(c11, c01);

					constexpr auto fracBits = 7;

					// lerp rows
					const short fxA = static_cast<short>(fracXA >> (scaleBits - fracBits));
					const short fxB = static_cast<short>(fracXB >> (scaleBits - fracBits));

					auto fxAvec = _mm_set1_epi16(fxA);
					auto fxBvec = _mm_set1_epi16(fxB);

					__m128i fracXVec = _mm_or_si128(_mm_slli_si128(fxBvec, 8), _mm_srli_si128(fxAvec, 8));

					__m128i c0 = _mm_add_epi16(c00, _mm_srai_epi16(_mm_mullo_epi16(d0, fracXVec), fracBits));
					__m128i c1 = _mm_add_epi16(c01, _mm_srai_epi16(_mm_mullo_epi16(d1, fracXVec), fracBits));

					// calc column differences
					__m128i d = _mm_sub_epi16(c1, c0);

					// lerp colums
					auto fracYVec = _mm_set1_epi16(static_cast<int16_t>(fracY >> (scaleBits - fracBits)));

					__m128i c = _mm_add_epi16(c0, _mm_srai_epi16(_mm_mullo_epi16(d, fracYVec), fracBits));

					// apply color
					if constexpr (Color)
					{
						__m128i colVec = _mm_set1_epi64x(*reinterpret_cast<int64_t*>(&col16));
						c = _mm_srli_epi16(_mm_mullo_epi16(c, colVec), 8);
					}

					// apply alpha blend if needed. Assumes premultiplied alpha
					if constexpr (AlphaBlend)
					{
						// load existing dst
						__m128i existing = _mm_cvtepu8_epi16(_mm_loadu_si64(reinterpret_cast<const int*>(dst)));
						// calc inv alpha
						__m128i alpha = _mm_shufflelo_epi16(c, _MM_SHUFFLE(3, 3, 3, 3));
						alpha = _mm_shufflehi_epi16(alpha, _MM_SHUFFLE(3, 3, 3, 3));
						__m128i invAlpha = _mm_sub_epi16(_mm_set1_epi16(255), alpha);
						// blend
						c = _mm_add_epi16(c, _mm_srli_epi16(_mm_mullo_epi16(existing, invAlpha), 8));
					}

					// convert back to 8-bit and store
					__m128i newDst8 = _mm_packus_epi16(c, _mm_setzero_si128());
					*reinterpret_cast<int64_t*>(dst) = _mm_cvtsi128_si64(newDst8);

					dst += 2;
				}
#endif
				for (; x<_dstW; ++x)
				{
					// bilinear filtering
					const int srcXi = srcX >> scaleBits;
					const int fracX = srcX - (srcXi << scaleBits);

					// fetch 4 texels
					const auto* c00 = _src.getColorPointer(srcXi, srcYi);
					const auto* c10 = c00 + 1;
					const auto* c01 = c00 + _src.paddedWidth;
					const auto* c11 = c01 + 1;

					// lerp upper row
					const auto c00f = toInt(*c00);
					const auto d0 = toInt(*c10) - c00f;
					const auto c0 = c00f + ((d0 * fracX) >> scaleBits);

					// lerp lower row
					const auto c01f = toInt(*c01);
					const auto d1 = toInt(*c11) - c01f;
					const auto c1 = c01f + ((d1 * fracX) >> scaleBits);

					// final lerp
					auto c = c0 + (((c1 - c0) * fracY) >> scaleBits);

					// apply color
					if constexpr (Color)
					{
						c = (c * col) >> 8;
					}

					// alpha blend if needed. Assumes premultiplied alpha in src
					if constexpr(AlphaBlend)
					{
						auto existing = toInt(*dst);
						const auto invAlpha = 255 - c.a;
						auto newDst = c + ((existing * invAlpha) >> 8);

						*dst = toByte(newDst);
					}
					else
					{
						*dst = toByte(c);
					}

					srcX += srcXStep;
					++dst;
				}

				srcY += srcYStep;
			}
		}

		template<bool HasScale, bool HasAlphaBlend, bool HasColor>
		static void blit(
			Image& _dst, const Image& _src,
			const int _srcX, const int _srcY, const int _srcW, const int _srcH,
			const int _dstX, const int _dstY, const int _dstW, const int _dstH, 
			const Colorb& _color
			)
		{
			if constexpr (HasScale)
			{
				blit<HasAlphaBlend, HasColor>(_dst, _src, _srcX, _srcY, _srcW, _srcH, _dstX, _dstY, _dstW, _dstH, _color);
			}
			else
			{
				blit<HasAlphaBlend, HasColor>(_dst, _dstX, _dstY, _src, _srcX, _srcY, _dstW, _dstH, _color);
			}
		}

		template<bool HasScale, bool HasAlphaBlend>
		static void blit(
			Image& _dst, const Image& _src,
			const int _srcX, const int _srcY, const int _srcW, const int _srcH,
			const int _dstX, const int _dstY, const int _dstW, const int _dstH, 
			const Colorb& _color, 
			bool hasColor
			)
		{
			if (hasColor) blit<HasScale, HasAlphaBlend, true >(_dst, _src, _srcX, _srcY, _srcW, _srcH, _dstX, _dstY, _dstW, _dstH, _color);
			else          blit<HasScale, HasAlphaBlend, false>(_dst, _src, _srcX, _srcY, _srcW, _srcH, _dstX, _dstY, _dstW, _dstH, _color);
		}

		template<bool HasScale>
		static void blit(
			Image& _dst, const Image& _src,
			const int _srcX, const int _srcY, const int _srcW, const int _srcH,
			const int _dstX, const int _dstY, const int _dstW, const int _dstH, 
			const Colorb& _color, 
			bool hasAlphablend, bool hasColor
			)
		{
			if (hasAlphablend) blit<HasScale, true >(_dst, _src, _srcX, _srcY, _srcW, _srcH, _dstX, _dstY, _dstW, _dstH, _color, hasColor);
			else               blit<HasScale, false>(_dst, _src, _srcX, _srcY, _srcW, _srcH, _dstX, _dstY, _dstW, _dstH, _color, hasColor);
		}

		static void blit(
			Image& _dst, const Image& _src,
			const int _srcX, const int _srcY, const int _srcW, const int _srcH,
			const int _dstX, const int _dstY, const int _dstW, const int _dstH, 
			const Colorb& _color, bool hasScale, bool hasAlphablend, bool hasColor)
		{
			if (hasScale) blit<true >(_dst, _src, _srcX, _srcY, _srcW, _srcH, _dstX, _dstY, _dstW, _dstH, _color, hasAlphablend, hasColor);
			else          blit<false>(_dst, _src, _srcX, _srcY, _srcW, _srcH, _dstX, _dstY, _dstW, _dstH, _color, hasAlphablend, hasColor);
		}

		template<bool AlphaBlend>
		static void fill(Image& _dst,
			const int _dstX, const int _dstY, const int _dstW, const int _dstH, const Colorb& _color)
		{
			const auto invAlpha = 255 - _color.a;

			const uint32_t fill = *reinterpret_cast<const uint32_t*>(&_color);
#ifdef HAVE_SSE
			const __m128i fillVec = _mm_set1_epi32(static_cast<int>(fill));
#endif

			for (int y=0; y<_dstH; ++y)
			{
				if constexpr (AlphaBlend)
				{
					auto* dst = _dst.getColorPointer(_dstX, _dstY + y);

					for (int x=0; x<_dstW; ++x)
					{
						// alpha blend
						Colori existingC = toInt(*dst);
						Colorb newDst = _color + toByte(((existingC * invAlpha) >> 8));
						*dst++ = newDst;
					}
				}
				else if constexpr (!AlphaBlend)
				{
					auto* dst = reinterpret_cast<uint32_t*>(_dst.getColorPointer(_dstX, _dstY + y));

					int x=0;
#ifdef HAVE_SSE
					for (; x<=_dstW - 16; x += 16)
					{
						_mm_storeu_si128(reinterpret_cast<__m128i*>(dst), fillVec); dst += 4;
						_mm_storeu_si128(reinterpret_cast<__m128i*>(dst), fillVec); dst += 4;
						_mm_storeu_si128(reinterpret_cast<__m128i*>(dst), fillVec); dst += 4;
						_mm_storeu_si128(reinterpret_cast<__m128i*>(dst), fillVec); dst += 4;
					}
					for (; x<=_dstW - 4; x += 4)
					{
						_mm_storeu_si128(reinterpret_cast<__m128i*>(dst), fillVec);
						dst += 4;
					}
#else
					for (; x<_dstW - 4; x += 4)
					{
						*dst++ = fill;
						*dst++ = fill;
						*dst++ = fill;
						*dst++ = fill;
					}
#endif

					for (;x<_dstW; ++x)
						*dst++ = fill;
				}
			}
		}
	}

	namespace
	{
		int roundToInt(const float _in)
		{
			// we have values > 0 only so that is fine
			return static_cast<int>(_in + 0.5f);
		}
	}

	RendererJuce::RendererJuce(Rml::CoreInstance& _coreInstance) : RenderInterface(_coreInstance)
	{
		static_assert(sizeof(rendererJuce::Colorb) == 4);
		static_assert(sizeof(Rml::Colourb) == 4);
	}

	RendererJuce::~RendererJuce()
	= default;

	Rml::CompiledGeometryHandle RendererJuce::CompileGeometry(const Rml::Span<const Rml::Vertex> _vertices, const Rml::Span<const int> _indices)
	{
		auto* g = new rendererJuce::Geometry();

		Rml::Vector2f posMin(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
		Rml::Vector2f posMax(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

		Rml::Vector2f uvMin = posMin;
		Rml::Vector2f uvMax = posMax;

		uint32_t redSum = 0;
		uint32_t greenSum = 0;
		uint32_t blueSum = 0;
		uint32_t alphaSum = 0;

		for (const auto& v : _vertices)
		{
			posMin.x = std::min(posMin.x, v.position.x);
			posMin.y = std::min(posMin.y, v.position.y);
			posMax.x = std::max(posMax.x, v.position.x);
			posMax.y = std::max(posMax.y, v.position.y);

			uvMin.x = std::min(uvMin.x, v.tex_coord.x);
			uvMin.y = std::min(uvMin.y, v.tex_coord.y);
			uvMax.x = std::max(uvMax.x, v.tex_coord.x);
			uvMax.y = std::max(uvMax.y, v.tex_coord.y);

			auto c = v.colour.ToNonPremultiplied();
			redSum += c.red;
			greenSum += c.green;
			blueSum += c.blue;
			alphaSum += c.alpha;
		}

//		g->color = juce::Colour(0xff808080);
		g->bounds = Rml::Rectanglef::FromCorners(Rml::Vector2f(posMin.x, posMin.y), Rml::Vector2f(posMax.x, posMax.y));
		g->uvBounds = Rml::Rectanglef::FromCorners(Rml::Vector2f(uvMin.x, uvMin.y), Rml::Vector2f(uvMax.x, uvMax.y));

		// try to extract quads
		size_t i = 0;
		for (; i <= _indices.size() - 6; i += 6)
		{
			int quadIndices[4] =
			{
				_indices[i],
				_indices[i],
				_indices[i],
				_indices[i],
			};

			int indicesUsed = 1;

			for (size_t j=0; j<6; ++j)
			{
				bool found = false;
				for (size_t k=0; k<indicesUsed; ++k)
				{
					if (_indices[i + j] == quadIndices[k])
					{
						found = true;
						break;
					}
				}
				if (!found)
				{
					if (indicesUsed < 4)
					{
						quadIndices[indicesUsed] = _indices[i + j];
						++indicesUsed;
					}
					else
					{
						indicesUsed = 5; // too many unique indices
						break;
					}
				}
			}

			if (indicesUsed == 4)
			{
				const auto& v0 = _vertices[quadIndices[0]];
				const auto& v1 = _vertices[quadIndices[1]];
				const auto& v2 = _vertices[quadIndices[2]];
				const auto& v3 = _vertices[quadIndices[3]];

				const Rml::Vector2f quadPosMin(
					std::min({ v0.position.x, v1.position.x, v2.position.x, v3.position.x }),
					std::min({ v0.position.y, v1.position.y, v2.position.y, v3.position.y })
				);

				const Rml::Vector2f quadPosMax(
					std::max({ v0.position.x, v1.position.x, v2.position.x, v3.position.x }),
					std::max({ v0.position.y, v1.position.y, v2.position.y, v3.position.y })
				);

				if (quadPosMax.x <= quadPosMin.y || quadPosMax.y <= quadPosMin.y)
					continue;

				const Rml::Vector2f quadUvMin(
					std::min({ v0.tex_coord.x, v1.tex_coord.x, v2.tex_coord.x, v3.tex_coord.x }),
					std::min({ v0.tex_coord.y, v1.tex_coord.y, v2.tex_coord.y, v3.tex_coord.y })
				);

				const Rml::Vector2f quadUvMax(
					std::max({ v0.tex_coord.x, v1.tex_coord.x, v2.tex_coord.x, v3.tex_coord.x }),
					std::max({ v0.tex_coord.y, v1.tex_coord.y, v2.tex_coord.y, v3.tex_coord.y })
				);

				uint32_t rSum = v0.colour.red + v1.colour.red + v2.colour.red + v3.colour.red;
				uint32_t gSum = v0.colour.green + v1.colour.green + v2.colour.green + v3.colour.green;
				uint32_t bSum = v0.colour.blue + v1.colour.blue + v2.colour.blue + v3.colour.blue;
				uint32_t aSum = v0.colour.alpha + v1.colour.alpha + v2.colour.alpha + v3.colour.alpha;

				auto quadColor = Rml::ColourbPremultiplied(
					static_cast<uint8_t>(rSum >> 2),
					static_cast<uint8_t>(gSum >> 2),
					static_cast<uint8_t>(bSum >> 2),
					static_cast<uint8_t>(aSum >> 2)
				);

				const auto hasColor = quadColor.red != 255 || quadColor.green != 255 || quadColor.blue != 255;

				g->quads.emplace_back(rendererJuce::Quad{
					Rml::Rectanglef::FromCorners(quadPosMin, quadPosMax),
					Rml::Rectanglef::FromCorners(quadUvMin, quadUvMax),
					Rml::Vector2f(
						(quadUvMax.x - quadUvMin.x) / (quadPosMax.x - quadPosMin.x),
						(quadUvMax.y - quadUvMin.y) / (quadPosMax.y - quadPosMin.y)
					),
					quadColor,
					hasColor });
			}
			else
			{
				break;
			}
		}

		// add remaining triangles to path
		for (; i <= _indices.size() - 3; i += 3)
		{
			const auto& v0 = _vertices[_indices[i]];
			const auto& v1 = _vertices[_indices[i+1]];
			const auto& v2 = _vertices[_indices[i+2]];
			assert(false);
		}

		return reinterpret_cast<Rml::CompiledGeometryHandle>(g);
	}

	void RendererJuce::ReleaseGeometry(Rml::CompiledGeometryHandle _geometry)
	{
		auto* p = reinterpret_cast<rendererJuce::Geometry*>(_geometry);
		delete p;
	}

	void RendererJuce::RenderGeometry(Rml::CompiledGeometryHandle _geometry, Rml::Vector2f _translation, Rml::TextureHandle _texture)
	{
		auto* p = reinterpret_cast<rendererJuce::Geometry*>(_geometry);
		if (!p)
			return;

		auto* img = reinterpret_cast<rendererJuce::Image*>(_texture);

		Rml::Rectanglei clip = Rml::Rectanglei::FromPositionSize(Rml::Vector2i(0, 0),  Rml::Vector2i(m_renderTarget->width, m_renderTarget->height));

		if (m_scissorEnabled)
			clip = clip.Intersect(m_scissorRegion);

		for (const auto& quad : p->quads)
		{
			int dstX = roundToInt(quad.position.Left() + _translation.x);
			int dstY = roundToInt(quad.position.Top() + _translation.y);
			int dstW = roundToInt(quad.position.Width());
			int dstH = roundToInt(quad.position.Height());

			// skip if outside clip
			if (dstX >= clip.Right())
				continue;
			if (dstY >= clip.Bottom())
				continue;
			if (dstX + dstW <= clip.Left())
				continue;
			if (dstY + dstH <= clip.Top())
				continue;

			float uvX = quad.uv.Left();
			float uvY = quad.uv.Top();
			float uvW = quad.uv.Width();
			float uvH = quad.uv.Height();

			// clip quad and adjust UVs accordingly
			if (dstX < clip.Left())
			{
				const auto d = clip.Left() - dstX;
				uvX += quad.uvPerPixel.x * static_cast<float>(d);
				uvW -= quad.uvPerPixel.x * static_cast<float>(d);
				dstX = clip.Left();
				dstW -= d;
			}

			if (dstY < clip.Top())
			{
				const auto d = clip.Top() - dstY;
				uvY += quad.uvPerPixel.y * static_cast<float>(d);
				uvH -= quad.uvPerPixel.y * static_cast<float>(d);
				dstY = clip.Top();
				dstH -= d;
			}

			if (dstX + dstW > clip.Right())
			{
				uvW -= quad.uvPerPixel.x * static_cast<float>((dstX + dstW) - clip.Right());
				dstW = clip.Right() - dstX;
			}

			if (dstY + dstH > clip.Bottom())
			{
				uvH -= quad.uvPerPixel.y * static_cast<float>((dstY + dstH) - clip.Bottom());
				dstH = clip.Bottom() - dstY;
			}

			const auto hasColor = quad.hasColor;
			rendererJuce::Colorb col { quad.color.red, quad.color.green, quad.color.blue, quad.color.alpha };

			if (img)
			{
				// define source rectangle in texture
				const int srcX = roundToInt(uvX * static_cast<float>(img->width));
				const int srcY = roundToInt(uvY * static_cast<float>(img->height));
				const int srcW = roundToInt(uvW * static_cast<float>(img->width));
				const int srcH = roundToInt(uvH * static_cast<float>(img->height));

				// use templated blitting function based on used features
				const auto hasScale = (srcW != dstW) || (srcH != dstH);
				const auto hasAlphaBlend = img->hasAlpha || col.a < 255;

				rendererJuce::blit(*m_renderTarget, *img, srcX, srcY, srcW, srcH, dstX, dstY, dstW, dstH, col, hasScale, hasAlphaBlend, hasColor);
			}
			else
			{
				// fill with solid color
				if (col.a < 255)
					rendererJuce::fill<true>(*m_renderTarget, dstX, dstY, dstW, dstH, col);
				else
					rendererJuce::fill<false>(*m_renderTarget, dstX, dstY, dstW, dstH, col);
			}
		}
	}

	Rml::TextureHandle RendererJuce::LoadTexture(Rml::Vector2i& _textureDimensions, const Rml::String& _source)
	{
		return {};
	}

	Rml::TextureHandle RendererJuce::GenerateTexture(Rml::Span<const uint8_t> _source, Rml::Vector2i _sourceDimensions)
	{
		// create juce::Image from raw RGBA data
		auto* img = new rendererJuce::Image();

		const auto srcH = _sourceDimensions.y;
		const auto srcW = _sourceDimensions.x;

		img->width = srcW;
		img->paddedWidth = rendererJuce::Image::padWidth(srcW);
		img->height = srcH;

		const auto dstW = rendererJuce::Image::padWidth(srcW);
		const auto dstH = rendererJuce::Image::padHeight(srcH);

		const size_t pixelCountSrc = srcW * srcH;
		const size_t pixelCountDst = dstW * dstH;

		size_t srcIndex = 0;

		img->data.resize(pixelCountDst * 4, 0x80);

		if (_source.size() == 4 * pixelCountSrc)
		{
			img->hasAlpha = true;

			for (int y=0; y<srcH; ++y)
			{
				auto dstIndex = (y * dstW) << 2;

				for (int x = 0; x < srcW; ++x)
				{
					img->data[dstIndex++] = _source[srcIndex++];
					img->data[dstIndex++] = _source[srcIndex++];
					img->data[dstIndex++] = _source[srcIndex++];
					img->data[dstIndex++] = _source[srcIndex++];
				}
			}
		}
		else if (_source.size() == 3 * pixelCountSrc)
		{
			img->hasAlpha = false;

			for (int y=0; y<srcH; ++y)
			{
				auto dstIndex = (y * dstW) << 2;

				for (int x = 0; x < srcW; ++x)
				{
					img->data[dstIndex++] = _source[srcIndex++];
					img->data[dstIndex++] = _source[srcIndex++];
					img->data[dstIndex++] = _source[srcIndex++];
					img->data[dstIndex++] = 0xff;
				}
			}
		}
		else
		{
			assert(false && "unknown image format");
			return reinterpret_cast<Rml::TextureHandle>(img);
		}

		// pad remaining rows/columns with neighbor pixels
		for (int y = srcH; y < dstH; ++y)
		{
			auto* src = img->getBytePointer(0, srcH - 1);
			auto* dst = img->getBytePointer(0, y);
			memcpy(dst, src, dstW * 4);
		}

		for (int x=srcW; x<dstW; ++x)
		{
			for (int y=0; y<dstH; ++y)
			{
				auto* src = img->getBytePointer(srcW - 1, y);
				auto* dst = img->getBytePointer(x, y);
				memcpy(dst, src, 4);
			}
		}

		return reinterpret_cast<Rml::TextureHandle>(img);
	}

	void RendererJuce::ReleaseTexture(Rml::TextureHandle _texture)
	{
		auto* img = reinterpret_cast<rendererJuce::Image*>(_texture);
		delete img;
	}

	void RendererJuce::EnableScissorRegion(bool _enable)
	{
		m_scissorEnabled = _enable;
		return;

		if (m_scissorEnabled)
			pushClip();
		else if (m_pushed)
		{
			m_graphics->restoreState();
			m_pushed = false;
		}
	}

	void RendererJuce::SetScissorRegion(Rml::Rectanglei _region)
	{
		m_scissorRegion = _region;
		return;
		if (m_scissorEnabled)
		{
			pushClip();
		}
		else if (m_pushed)
		{
			m_graphics->restoreState();
			m_pushed = false;
		}
	}

	void RendererJuce::pushClip()
	{
		if (m_pushed)
			m_graphics->restoreState();

		m_graphics->saveState();
		m_pushed = true;

		m_graphics->reduceClipRegion(juce::Rectangle(
			m_scissorRegion.Left(),
			m_scissorRegion.Top(),
			m_scissorRegion.Width(),
			m_scissorRegion.Height()
		));
	}

	void RendererJuce::beginFrame(juce::Graphics& _g)
	{
		m_graphics = &_g;
		m_graphics->setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);

		const auto width = _g.getClipBounds().getWidth();
		const auto height = _g.getClipBounds().getHeight();

		m_scissorRegion = Rml::Rectanglei::FromPositionSize(Rml::Vector2i(0,0), Rml::Vector2i(width, height));

		if (!m_renderTarget || m_renderTarget->width != width || m_renderTarget->height != height)
		{
			m_renderTarget.reset(new rendererJuce::Image());
			m_renderTarget->width = width;
			m_renderTarget->paddedWidth = rendererJuce::Image::padWidth(width);
			m_renderTarget->height = height;
			m_renderTarget->data.resize((rendererJuce::Image::padWidth(width) * rendererJuce::Image::padHeight(height)) << 2);

			m_renderImage.reset(new juce::Image(juce::Image::RGB, width, height, false));
		}
	}

	namespace
	{
		template<typename T>
		constexpr int getAlphaComponentIndex()
		{
			if constexpr (std::is_same_v<juce::PixelARGB, T>)
				return juce::PixelARGB::indexA;
			if constexpr (std::is_same_v<juce::PixelRGB, T>)
			{
				if constexpr(juce::PixelRGB::indexR != 3 && juce::PixelRGB::indexG != 3 && juce::PixelRGB::indexB != 3)
					return 3;
				else if constexpr(juce::PixelRGB::indexR != 0 && juce::PixelRGB::indexG != 0 && juce::PixelRGB::indexB != 0)
					return 0;
			}
			return -1;
		}

		// default implementation that is slow but safe
		template<typename PixelDataType>
		void copyToBitmap(const juce::Image::BitmapData& _dst, const rendererJuce::Image& _src, int _x, int _y, int _width, int _height)
		{
			const auto yEnd = _y + _height;

			for (int y=_y; y<yEnd; ++y)
			{
				const auto* src = _src.getColorPointer(_x, y);

				auto* dst = _dst.getPixelPointer(_x, y);

				for (int x=0; x<_width; ++x, dst += _dst.pixelStride)
				{
					auto* p = reinterpret_cast<PixelDataType*>(dst);
					p->setARGB(0xff, src->r, src->g, src->b);
					++src;
				}
			}
		}

		// optimized version for 4-byte stride target bitmaps
		template<typename PixelDataType>
		void copyToBitmap4(const juce::Image::BitmapData& _dst, const rendererJuce::Image& _src, int _x, int _y, int _width, int _height)
		{
			const auto yEnd = _y + _height;

			for (int y=_y; y<yEnd; ++y)
			{
				const auto* src = _src.getColorPointer(_x, y);

				auto* dst = _dst.getPixelPointer(_x, y);
#ifdef HAVE_SSE
				// use SSE to speed up RGBA->ARGB or RGBA->BGRA conversion
				auto shuffleMask = []
				{
					constexpr auto shuffleR = hostEndian() == HostEndian::Big ? static_cast<int8_t>(PixelDataType::indexR) : getAlphaComponentIndex<PixelDataType>()   ;
					constexpr auto shuffleG = hostEndian() == HostEndian::Big ? static_cast<int8_t>(PixelDataType::indexG) : static_cast<int8_t>(PixelDataType::indexB);
					constexpr auto shuffleB = hostEndian() == HostEndian::Big ? static_cast<int8_t>(PixelDataType::indexB) : static_cast<int8_t>(PixelDataType::indexG);
					constexpr auto shuffleA = hostEndian() == HostEndian::Big ? getAlphaComponentIndex<PixelDataType>()    : static_cast<int8_t>(PixelDataType::indexR);

					const __m128i shuffleMask = _mm_set_epi8(
					         shuffleR + 12, shuffleG + 12, shuffleB + 12, shuffleA + 12,
					         shuffleR + 8 , shuffleG + 8 , shuffleB + 8 , shuffleA + 8,
					         shuffleR + 4 , shuffleG + 4 , shuffleB + 4 , shuffleA + 4,
					         shuffleR + 0 , shuffleG + 0 , shuffleB + 0 , shuffleA + 0
					    );
					return shuffleMask;
				};

				// process 16 pixels at a time
				int x = 0;
				for (; x<_width - 16; x += 16)
				{
					_mm_storeu_si128(reinterpret_cast<__m128i*>(dst), _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(src)), shuffleMask()));
					src += 4; dst += 4 * 4;
					_mm_storeu_si128(reinterpret_cast<__m128i*>(dst), _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(src)), shuffleMask()));
					src += 4; dst += 4 * 4;
					_mm_storeu_si128(reinterpret_cast<__m128i*>(dst), _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(src)), shuffleMask()));
					src += 4; dst += 4 * 4;
					_mm_storeu_si128(reinterpret_cast<__m128i*>(dst), _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(src)), shuffleMask()));
					src += 4; dst += 4 * 4;
				}

				// process 4 pixels at a time
				for (; x<_width - 4; x += 4, src += 4, dst += 4 * 4)
				{
					_mm_storeu_si128(reinterpret_cast<__m128i*>(dst), _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(src)), shuffleMask()));
				}

				// process remaining pixels
				for (; x<_width; ++x, ++src, dst += 4)
				{
					_mm_storeu_si32(dst, _mm_shuffle_epi8(_mm_loadu_si32(reinterpret_cast<const int*>(src)), shuffleMask()));
				}
#else
				for (int x=0; x<_width; ++x, ++src, dst += 4)
				{
					auto* p = reinterpret_cast<PixelDataType*>(dst);
					p->setARGB(0xff, src->r, src->g, src->b);
				}
#endif
			}
		}

		static_assert(getAlphaComponentIndex<juce::PixelRGB>() != -1);
		static_assert(getAlphaComponentIndex<juce::PixelARGB>() != -1);

		void copyToBitmap(const juce::Image::BitmapData& _dst, const rendererJuce::Image& _src)
		{
			const auto w = std::min(_src.width, _dst.width);
			const auto h = std::min(_src.height, _dst.height);

			if (_dst.pixelStride == 4)
			{
				if (_dst.pixelFormat == juce::Image::ARGB)
					copyToBitmap<juce::PixelARGB>(_dst, _src, 0, 0, w, h);
				else if (_dst.pixelFormat == juce::Image::RGB)
					copyToBitmap4<juce::PixelRGB>(_dst, _src, 0, 0, w, h);
			}
			else if (_dst.pixelFormat == juce::Image::RGB)
				copyToBitmap<juce::PixelRGB>(_dst, _src, 0, 0, w, h);
			else if (_dst.pixelFormat == juce::Image::ARGB)
				copyToBitmap<juce::PixelARGB>(_dst, _src, 0, 0, w, h);
		}
	}

	void RendererJuce::endFrame(const juce::Image& _renderTarget)
	{
		// copy render target to juce::Image
		{
			const juce::Image::BitmapData dstBitmapData(_renderTarget.isNull() ? *m_renderImage : _renderTarget, juce::Image::BitmapData::writeOnly);

			if (_renderTarget.isNull())
			{
				copyToBitmap(dstBitmapData, *m_renderTarget);

				auto& context = m_graphics->getInternalContext();

				context.drawImage(*m_renderImage, juce::AffineTransform());
			}
			else
			{
				copyToBitmap(dstBitmapData, *m_renderTarget);
			}
		}

		m_graphics = nullptr;
	}
}
