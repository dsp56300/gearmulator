#include "rmlRendererJuce.h"

#include <algorithm>
#include <cassert>

#include "juce_graphics/juce_graphics.h"

namespace juceRmlUi
{
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
			juce::Colour color;
			bool hasColor;
		};

		struct Geometry
		{
			Rml::Rectanglef bounds;
			Rml::Rectanglef uvBounds;
			std::vector<Quad> quads;
			juce::Path path;
		};

		template<typename T>
		struct Color
		{
			T r,g,b,a;

			Color operator + (const Color& _c) const noexcept { return Color{ r + _c.r, g + _c.g, b + _c.b, a + _c.a }; }
			Color operator - (const Color& _c) const noexcept { return Color{ r - _c.r, g - _c.g, b - _c.b, a - _c.a }; }

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
				static_assert(std::is_same_v<T,uint8_t>);
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
				static_assert(std::is_same_v<T,uint8_t>);
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
				static_assert(std::is_same_v<T,float>);
				return Color<float>{r * _v, g * _v, b * _v, a * _v};
			}
		};

		using Colorb = Color<uint8_t>;
		using Colorf = Color<float>;

		float toFloat(uint8_t _v) noexcept { return _v; }
		uint8_t toByte(float _v) noexcept { return static_cast<uint8_t>(_v); }

		Colorf toFloat(const Colorb& _c) noexcept { return Colorf{ toFloat(_c.r), toFloat(_c.g), toFloat(_c.b), toFloat(_c.a)}; }
		Colorb toByte(const Colorf& _c) noexcept { return Colorb{ toByte(_c.r), toByte(_c.g), toByte(_c.b), toByte(_c.a) }; }

		struct Image
		{
			static constexpr int PadW = 4;	// up to N bytes of padding to ensure we have pixel groups of width N, required for some SIMD ops
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

		static void blit(Image& _dst, const Image& _src, const int _srcX, const int _srcY, const int _srcW, const int _srcH, const Colorb& _color)
		{
			const Colorb* src = _src.getColorPointer(_srcX, _srcY);
			Colorb* dst = _dst.getColorPointer(0,0);

			for (int y = 0; y < _srcH; ++y)
			{
				for (int x = 0; x < _srcW; ++x, ++src, ++dst)
					*dst = *src * _color;

				src += _src.paddedWidth;
				dst += _dst.paddedWidth;
			}
		}

		static void blit(Image& _dst, const Image& _src, const int _srcX, const int _srcY, const int _srcW, const int _srcH)
		{
			const auto* src = _src.getColorPointer(_srcX, _srcY);
			auto* dst = _dst.getColorPointer(0,0);

			for (int y = 0; y < _srcH; ++y)
			{
				memcpy(dst, src, _srcW * sizeof(Colorb));
				src += _src.paddedWidth;
				dst += _dst.paddedWidth;
			}
		}

		static void blit(Image& _dst, const Image& _src,
			const int _srcX, const int _srcY, const int _srcW, const int _srcH,
			const int _dstX, const int _dstY, const int _dstW, const int _dstH)
		{
			float srcY = static_cast<float>(_srcY);

			const float srcXStep = static_cast<float>(_srcW) / static_cast<float>(_dstW);
			const float srcYStep = static_cast<float>(_srcH) / static_cast<float>(_dstH);

			for (int y=0; y<_dstH; ++y)
			{
				const int srcYi = static_cast<int>(srcY);
				const float fracY = srcY - static_cast<float>(srcYi);

				float srcX = static_cast<float>(_srcX);

				auto* dst = _dst.getColorPointer(_dstX, _dstY + y);

				for (int x=0; x<_dstW; ++x)
				{
					// bilinear filtering
					const int srcXi = static_cast<int>(srcX);
					const float fracX = srcX - static_cast<float>(srcXi);

					const Colorb* c00 = _src.getColorPointer(srcXi, srcYi);
					const Colorb* c10 = c00 + 1;
					const Colorb* c01 = c00 + _src.paddedWidth;
					const Colorb* c11 = c01 + 1;

					const auto c00f = toFloat(*c00);
					const auto d0 = toFloat(*c10) - c00f;
					const auto c0 = c00f + d0 * fracX;

					const auto c01f = toFloat(*c01);
					const auto d1 = toFloat(*c11) - c01f;
					const auto c1 = c01f + d1 * fracX;

					const auto c = c0 + (c1 - c0) * fracY;

					*dst = toByte(c);

					srcX += srcXStep;
					++dst;
				}

				srcY += srcYStep;
			}
		}
	}

	namespace
	{
		constexpr float g_oneDiv255 = 0.003921568627451f;

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

		g->path.clear();

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

				auto quadColor = juce::Colour(
					static_cast<uint8_t>(rSum >> 2),
					static_cast<uint8_t>(gSum >> 2),
					static_cast<uint8_t>(bSum >> 2),
					static_cast<uint8_t>(aSum >> 2)
				);

				const auto hasColor = quadColor.getRed() != 255 || quadColor.getGreen() != 255 || quadColor.getBlue() != 255;

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
			g->path.addTriangle(v0.position.x, v0.position.y, v1.position.x, v1.position.y, v2.position.x, v2.position.y);
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

		if (!img)
		{
			for (const auto& quad : p->quads)
			{
				m_graphics->setColour(quad.color);
				juce::Rectangle r(quad.position.Left(), quad.position.Top(), quad.position.Width(), quad.position.Height());
				r = r.translated(_translation.x, _translation.y);
				m_graphics->fillRect(r);
			}
			return;
		}

		Rml::Rectanglei clip;

		if (m_scissorEnabled)
			clip = m_scissorRegion;
		else
			clip = Rml::Rectanglei::FromPositionSize(Rml::Vector2i(0, 0), Rml::Vector2i(m_renderTarget->width, m_renderTarget->height));

		for (const auto& quad : p->quads)
		{
			float uvX = quad.uv.Left();
			float uvY = quad.uv.Top();
			float uvW = quad.uv.Width();
			float uvH = quad.uv.Height();


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

			// define source rectangle in texture
			const int srcX = roundToInt(uvX * static_cast<float>(img->width));
			const int srcY = roundToInt(uvY * static_cast<float>(img->height));
			const int srcW = roundToInt(uvW * static_cast<float>(img->width));
			const int srcH = roundToInt(uvH * static_cast<float>(img->height));

			// use templated blitting function based on used features
			const auto requiresScale = (srcW != dstW) || (srcH != dstH);
			const auto requiresColor = quad.hasColor;
			const auto requiresAlphaBlend = img->hasAlpha;

			if (!quad.hasColor)
			{
//				m_graphics->setOpacity(static_cast<float>(quad.color.getAlpha()) * g_oneDiv255);

				rendererJuce::blit(*m_renderTarget, *img,
							srcX, srcY, srcW, srcH,
							dstX, dstY, dstW, dstH);//,
			}
			else
			{
				rendererJuce::blit(*m_renderTarget, *img,
							srcX, srcY, srcW, srcH,
							dstX, dstY, dstW, dstH);//,
//					quad.color);
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
		auto* img = reinterpret_cast<juce::Image*>(_texture);
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

	void RendererJuce::endFrame()
	{
		// copy render target to juce::Image
		{
			const auto width = m_renderTarget->width;
			const auto height = m_renderTarget->height;

			juce::Image::BitmapData dstBitmapData(*m_renderImage, juce::Image::BitmapData::writeOnly);

			for (int y=0; y<height; ++y)
			{
				const auto* src = m_renderTarget->getColorPointer(0, y);

				for (int x=0; x<width; ++x)
				{
					auto* dst = reinterpret_cast<juce::PixelRGB*>(dstBitmapData.getPixelPointer(x, y));
					dst->setARGB(0xff, src->r, src->g, src->b);
					++src;
				}
			}
		}

		m_graphics->getInternalContext().drawImage(*m_renderImage, juce::AffineTransform());

		m_graphics = nullptr;
	}
}
