#include "rmlRendererJuce.h"

#include <algorithm>

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
			int quadIndices[4] = {
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

				g->quads.emplace_back(rendererJuce::Quad{ Rml::Rectanglef::FromCorners(quadPosMin, quadPosMax), Rml::Rectanglef::FromCorners(quadUvMin, quadUvMax), quadColor, hasColor });
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

		auto* img = reinterpret_cast<juce::Image*>(_texture);

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

		for (const auto& quad : p->quads)
		{
			const int srcX = roundToInt(quad.uv.Left() * static_cast<float>(img->getWidth()));
			const int srcY = roundToInt(quad.uv.Top() * static_cast<float>(img->getHeight()));
			const int srcW = roundToInt(quad.uv.Width() * static_cast<float>(img->getWidth()));
			const int srcH = roundToInt(quad.uv.Height() * static_cast<float>(img->getHeight()));

			const int dstX = roundToInt(quad.position.Left() + _translation.x);
			const int dstY = roundToInt(quad.position.Top() + _translation.y);
			const int dstW = roundToInt(quad.position.Width());
			const int dstH = roundToInt(quad.position.Height());

			juce::Rectangle dst(dstX, dstY, dstW, dstH);
			if (!m_graphics->clipRegionIntersects(dst))
				continue;

			if (!quad.hasColor)
			{
				m_graphics->setOpacity(static_cast<float>(quad.color.getAlpha()) * g_oneDiv255);

				m_graphics->drawImage(*img, 
					dstX, dstY, dstW, dstH,
					srcX, srcY, srcW, srcH
					);
				// if the destination size is smaller than the source size, we blit to the temporary image and downscale, then apply color
				// On the other hand, if the destination size is larger than the source size, we blit directly with color modulation
			}
			else
			{
				auto tempImg = blit(*img, srcX, srcY, srcW, srcH, quad.color);

				m_graphics->drawImage(tempImg, 
					dstX, dstY, dstW, dstH,
					0, 0, srcW, srcH
					);
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
		juce::Image* img = new juce::Image(juce::Image::ARGB, _sourceDimensions.x, _sourceDimensions.y, false);
		{
			juce::Image::BitmapData data(*img, juce::Image::BitmapData::writeOnly);

			const size_t pixelCount = _sourceDimensions.x * _sourceDimensions.y;

			size_t srcIndex = 0;

			if (_source.size() == 4 * pixelCount)
			{
				for (int y = 0; y < _sourceDimensions.y; ++y)
				{
					for (int x = 0; x < _sourceDimensions.x; ++x)
					{
						const uint8_t r = _source[srcIndex++];
						const uint8_t g = _source[srcIndex++];
						const uint8_t b = _source[srcIndex++];
						const uint8_t a = _source[srcIndex++];
						const uint32_t pixel = (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
						data.setPixelColour(x, y, juce::Colour(pixel));
					}
				}
			}
			else if (_source.size() == 3 * pixelCount)
			{
				for (int y = 0; y < _sourceDimensions.y; ++y)
				{
					for (int x = 0; x < _sourceDimensions.x; ++x)
					{
						const uint8_t r = _source[srcIndex++];
						const uint8_t g = _source[srcIndex++];
						const uint8_t b = _source[srcIndex++];
						constexpr uint8_t a = 0xff;
						const uint32_t pixel = (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
						data.setPixelColour(x, y, juce::Colour(pixel));
					}
				}
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

	juce::Image& RendererJuce::blit(const juce::Image& _img, int _srcX, int _srcY, int _srcW, int _srcH, const juce::Colour& _color)
	{
		if (!m_tempImage || m_tempImage->getWidth() < _srcW || m_tempImage->getHeight() < _srcH)
		{
			m_tempImage.reset(new juce::Image(juce::Image::ARGB, _srcW, _srcH, false));
		}

		juce::Image::BitmapData srcData(_img, juce::Image::BitmapData::readOnly);
		juce::Image::BitmapData dstData(*m_tempImage, juce::Image::BitmapData::writeOnly);

		uint32_t mulr = _color.getRed();
		uint32_t mulg = _color.getGreen();
		uint32_t mulb = _color.getBlue();
		uint32_t mula = _color.getAlpha();

		for (int y = 0; y < _srcH; ++y)
		{
			juce::PixelARGB* src = reinterpret_cast<juce::PixelARGB*>(srcData.getLinePointer(_srcY + y)) + _srcX;
			juce::PixelARGB* dst = reinterpret_cast<juce::PixelARGB*>(dstData.getLinePointer(y));

			for (int x = 0; x < _srcW; ++x, ++src, ++dst)
			{
				auto r = static_cast<uint8_t>((static_cast<uint32_t>(src->getRed()) * mulr) >> 8);
				auto g = static_cast<uint8_t>((static_cast<uint32_t>(src->getGreen()) * mulg) >> 8);
				auto b = static_cast<uint8_t>((static_cast<uint32_t>(src->getBlue()) * mulb) >> 8);
				auto a = static_cast<uint8_t>((static_cast<uint32_t>(src->getAlpha()) * mula) >> 8);

				dst->setARGB(a,r,g,b);
			}
		}

		return *m_tempImage;
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
		m_pushed = false;
		m_scissorRegion = Rml::Rectanglei::FromPositionSize(Rml::Vector2i(0,0), Rml::Vector2i(_g.getClipBounds().getWidth(), _g.getClipBounds().getHeight()));

		m_graphics = &_g;
		m_graphics->setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
	}

	void RendererJuce::endFrame()
	{
		m_graphics = nullptr;
	}
}
