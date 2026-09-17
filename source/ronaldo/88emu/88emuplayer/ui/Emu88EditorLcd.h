#pragma once

#include "88lib/hardwareDevice.h"
#include "88emuplayer/ui/panel.hpp"
#include "juceRmlUi/rmlElemCanvas.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace emu88Player
{
	class HardwareLcd final
	{
	public:
		explicit HardwareLcd(Rml::Element& _element)
		{
			m_canvas = juceRmlUi::ElemCanvas::create(&_element);
			m_canvas->setClearEveryFrame(true);
			m_canvas->setFixedTextureSize(sc88panel::kWidth, sc88panel::kHeight);
			m_canvas->setRepaintGraphicsCallback(
				[this](const juce::Image& _image, juce::Graphics& _graphics)
				{
					_graphics.fillAll(juce::Colours::transparentBlack);
					if(!m_source.isValid())
						return;
					_graphics.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
					_graphics.drawImageWithin(m_source, 0, 0, _image.getWidth(), _image.getHeight(),
					                          juce::RectanglePlacement::centred, false);
				});
		}

		// _screen is 0 for the board's only display, 1 for the second panel of a board that has
		// one (the CM-64, whose halves each bring their own).
		void reset(const emu88Lib::DeviceModel _model, const unsigned _screen = 0)
		{
			m_source = {};
			m_graphicBase = {};
			m_shadow = {};
			const auto geometry = geometryFor(_model, _screen);
			m_style = geometry.style;
			if(!emu88Lib::deviceHasLcd(_model) || (_screen && !emu88Lib::deviceHasSecondLcd(_model)))
			{
				// Nothing to draw: these boards' artwork prints NO DISPLAY, and the stylesheet hides
				// the canvas (.modelNoDisplay), as it does the second panel nobody else has.
				m_canvas->repaint();
				return;
			}
			if(geometry.width)
				composeGraphic(geometry.width, geometry.height, nullptr);
			else
				m_canvas->setFixedTextureSize(sc88panel::kWidth, sc88panel::kHeight);
			m_canvas->repaint();
		}

		void setSnapshot(const emu88Lib::HardwareDevice::DisplaySnapshot::Screen& _snapshot)
		{
			if(_snapshot.type == emu88Lib::HardwareDevice::DisplaySnapshot::Type::Graphic)
			{
				if(!_snapshot.width || !_snapshot.height ||
				   _snapshot.mono.size() != static_cast<size_t>(_snapshot.width) * _snapshot.height)
					return;
				composeGraphic(_snapshot.width, _snapshot.height, _snapshot.displayOn ? _snapshot.mono.data() : nullptr);
				m_canvas->repaint();
				return;
			}
			if(_snapshot.type != emu88Lib::HardwareDevice::DisplaySnapshot::Type::Character)
				return;
			m_pixels.resize(static_cast<size_t>(sc88panel::kWidth) * sc88panel::kHeight);
			sc88panel::renderOverlay(m_pixels.data(), _snapshot.ddRam.data(), _snapshot.cgRam.data(),
			                         !_snapshot.displayOn, kLcdOn, kLcdOffOverlay);
			m_source = juce::Image(juce::Image::ARGB, sc88panel::kWidth, sc88panel::kHeight, false);
			const juce::Image::BitmapData target(m_source, juce::Image::BitmapData::writeOnly);
			for(int y = 0; y < sc88panel::kHeight; ++y)
				for(int x = 0; x < sc88panel::kWidth; ++x)
					target.setPixelColour(x, y, juce::Colour(
						m_pixels[static_cast<size_t>(y) * sc88panel::kWidth + x]));
			m_canvas->repaint();
		}

	private:
		static constexpr uint32_t kLcdGlass = 0xffff6f0fu;
		static constexpr uint32_t kLcdOffOverlay = 0x38000000u;
		static constexpr uint32_t kLcdOn = 0xff000000u;

		// How a graphic panel is drawn. Each snapshot pixel owns a pitch x pitch square of the texture and
		// a dot fills dotSize of it. cellWidth/cellHeight are a character cell in snapshot pixels, whose
		// last column and row are the gap to the next cell, for boards whose snapshot keeps those gaps; 0
		// means every pixel is a dot. A texture size of 0 is the snapshot's own size at the pitch;
		// otherwise the texture covers the whole window and the dots start at originX/originY within it.
		struct GraphicStyle
		{
			uint32_t glass;
			uint32_t off;
			uint32_t on;
			int pitch;
			int dotSize;
			int cellWidth;
			int cellHeight;
			int textureWidth;
			int textureHeight;
			int originX;
			int originY;
			// The window in the skin, in dp. Zero means no inner shadow is drawn over it.
			int windowWidthDp;
			int windowHeightDp;
		};
		// The SC-8850: lit dots straight onto the orange glass, edge to edge.
		static constexpr GraphicStyle kSc8850Style{kLcdGlass, kLcdGlass, kLcdOn, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0};
		// The CM-32P fills its 218 x 41 dp window at 800 x 150: a yellow-green backlight with black lit
		// dots and green unlit ones, 6 px on an 8 px pitch. Its HD44780 snapshot has 6x9 cells, each
		// ending in the gap to the next; the origin insets the dots within the window.
		static constexpr GraphicStyle kCm32pStyle{0xff51be03u, 0xff00b578u, 0xff000000u, 8, 6, 6, 9, 800, 150, 25, 11,
		                                          218, 41};
		// The CM-32L's window is the same width but half the height, 218 x 20 dp at 1000 x 92. Its
		// SED1200 puts 20 columns where the CM-32P has 16, so the same 6 px dot on an 8 px pitch
		// fills the same share of the width and the glyphs come out proportionally smaller - which
		// is how the two read side by side on the CM-64 bezel that carries both.
		static constexpr GraphicStyle kCm32lStyle{0xff51be03u, 0xff00b578u, 0xff000000u, 8, 6, 6, 9, 1000, 92, 20, 10,
		                                          218, 20};

		// What each panel is drawn as: its dot grid, and the style that paints it. A zero width
		// is the SC-88 family's character panel, which sc88panel renders instead.
		struct ScreenGeometry
		{
			GraphicStyle style;
			int width;
			int height;
		};

		static constexpr ScreenGeometry kNoGraphic{kSc8850Style, 0, 0};
		static constexpr ScreenGeometry kCm32pPanel{kCm32pStyle, 96, 18};
		static constexpr ScreenGeometry kCm32lPanel{kCm32lStyle, 120, 9};

		static ScreenGeometry geometryFor(const emu88Lib::DeviceModel _model, const unsigned _screen)
		{
			using emu88Lib::DeviceModel;
			// The CM-64 is a CM-32P above a CM-32L, so its two panels are exactly those two boards'.
			if(_model == DeviceModel::Cm64)
				return _screen == 0 ? kCm32pPanel : kCm32lPanel;
			if(_screen)
				return kNoGraphic;	// nothing else has a second panel
			if(_model == DeviceModel::Cm32p) return kCm32pPanel;
			if(_model == DeviceModel::Cm32l) return kCm32lPanel;
			if(_model == DeviceModel::Sc8850) return {kSc8850Style, 160, 64};
			return kNoGraphic;
		}

		// The glass with its unlit dots, the lit dots of _mono (_width x _height, row major; null leaves
		// them all unlit), then the window's shadow over everything.
		void composeGraphic(const int _width, const int _height, const uint8_t* _mono)
		{
			if(!m_graphicBase.isValid() || _width != m_graphicWidth || _height != m_graphicHeight)
				createGraphicBase(_width, _height);
			m_source = m_graphicBase.createCopy();
			{
				juce::Graphics pixels(m_source);
				if(_mono)
				{
					pixels.setColour(juce::Colour(m_style.on));
					for(int y = 0; y < _height; ++y)
						for(int x = 0; x < _width; ++x)
							if(_mono[static_cast<size_t>(y) * _width + x])
								fillDot(pixels, x, y);
				}
				if(m_shadow.isValid())
					pixels.drawImageAt(m_shadow, 0, 0);
			}
			m_canvas->setFixedTextureSize(m_source.getWidth(), m_source.getHeight());
		}

		void createGraphicBase(const int _width, const int _height)
		{
			m_graphicWidth = _width;
			m_graphicHeight = _height;
			const int textureWidth = m_style.textureWidth ? m_style.textureWidth : _width * m_style.pitch;
			const int textureHeight = m_style.textureHeight ? m_style.textureHeight : _height * m_style.pitch;
			m_graphicBase = juce::Image(juce::Image::ARGB, textureWidth, textureHeight, true);
			m_shadow = m_style.windowWidthDp
				? createWindowShadow(textureWidth, textureHeight, m_style) : juce::Image();
			juce::Graphics pixels(m_graphicBase);
			pixels.fillAll(juce::Colour(m_style.glass));
			if(m_style.off == m_style.glass)
				return;
			// Unlit dots stay visible, as they do on the glass.
			pixels.setColour(juce::Colour(m_style.off));
			for(int y = 0; y < _height; ++y)
				for(int x = 0; x < _width; ++x)
					if(isDot(x, y))
						fillDot(pixels, x, y);
		}

		bool isDot(const int _x, const int _y) const
		{
			return m_style.cellWidth == 0 ||
			       (_x % m_style.cellWidth < m_style.cellWidth - 1 && _y % m_style.cellHeight < m_style.cellHeight - 1);
		}

		void fillDot(juce::Graphics& _graphics, const int _x, const int _y) const
		{
			_graphics.fillRect(m_style.originX + _x * m_style.pitch, m_style.originY + _y * m_style.pitch,
			                   m_style.dotSize, m_style.dotSize);
		}

		// The CM skins' LCD windows have two inner shadows: black at 30% offset by (+2, +2) dp and at
		// 15% by (-2, -2) dp, each blurred by a 1 dp Gaussian. Blurring the window's offset copy has a
		// closed form, so the overlay is computed for the texture rather than stored. The offsets are
		// in dp, so the window's own size has to come with it.
		static juce::Image createWindowShadow(const int _width, const int _height, const GraphicStyle& _style)
		{
			const auto windowWidth = static_cast<float>(_style.windowWidthDp);
			const auto windowHeight = static_cast<float>(_style.windowHeightDp);
			const float scale = 1.0f / std::sqrt(2.0f);	// 1 / (sigma * sqrt(2)) with sigma = 1 dp
			// The share of [_lo, _hi] that a Gaussian centred on _p covers.
			const auto covered = [scale](const float _p, const float _lo, const float _hi)
			{
				return 0.5f * (std::erf((_hi - _p) * scale) - std::erf((_lo - _p) * scale));
			};
			juce::Image shadow(juce::Image::ARGB, _width, _height, true);
			{
				const juce::Image::BitmapData data(shadow, juce::Image::BitmapData::writeOnly);
				for(int y = 0; y < _height; ++y)
				{
					const float py = (static_cast<float>(y) + 0.5f) * windowHeight / static_cast<float>(_height);
					for(int x = 0; x < _width; ++x)
					{
						const float px = (static_cast<float>(x) + 0.5f) * windowWidth / static_cast<float>(_width);
						const float topLeft = 0.30f * (1.0f - covered(px, 2.0f, windowWidth + 2.0f) *
						                                      covered(py, 2.0f, windowHeight + 2.0f));
						const float bottomRight = 0.15f * (1.0f - covered(px, -2.0f, windowWidth - 2.0f) *
						                                          covered(py, -2.0f, windowHeight - 2.0f));
						data.setPixelColour(x, y, juce::Colours::black.withAlpha(
							1.0f - (1.0f - topLeft) * (1.0f - bottomRight)));
					}
				}
			}
			return shadow;
		}


		juceRmlUi::ElemCanvas* m_canvas = nullptr;
		juce::Image m_source;
		juce::Image m_graphicBase;
		juce::Image m_shadow;
		int m_graphicWidth = 0;
		int m_graphicHeight = 0;
		GraphicStyle m_style = kSc8850Style;
		std::vector<uint32_t> m_pixels;
	};
}
