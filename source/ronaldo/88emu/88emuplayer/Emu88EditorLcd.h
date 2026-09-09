#pragma once

#include "88lib/hardwareDevice.h"
#include "panel.hpp"
#include "juceRmlUi/rmlElemCanvas.h"

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

		void reset(const emu88Lib::DeviceModel _model)
		{
			m_source = {};
			if(_model == emu88Lib::DeviceModel::Sc8850)
			{
				createGraphicBase(160, 64);
				m_source = m_graphicBase.createCopy();
				m_canvas->setFixedTextureSize(160 * kGraphicPixelPitch, 64 * kGraphicPixelPitch);
			}
			else
				m_canvas->setFixedTextureSize(sc88panel::kWidth, sc88panel::kHeight);
			m_canvas->repaint();
		}

		void setSnapshot(const emu88Lib::HardwareDevice::DisplaySnapshot& _snapshot)
		{
			if(_snapshot.type == emu88Lib::HardwareDevice::DisplaySnapshot::Type::Graphic)
			{
				if(!_snapshot.width || !_snapshot.height ||
				   _snapshot.mono.size() != static_cast<size_t>(_snapshot.width) * _snapshot.height)
					return;
				const int imageWidth = _snapshot.width * kGraphicPixelPitch;
				const int imageHeight = _snapshot.height * kGraphicPixelPitch;
				if(!m_graphicBase.isValid() || m_graphicBase.getWidth() != imageWidth ||
				   m_graphicBase.getHeight() != imageHeight)
					createGraphicBase(_snapshot.width, _snapshot.height);
				m_source = m_graphicBase.createCopy();
				if(_snapshot.displayOn)
				{
					juce::Graphics pixels(m_source);
					pixels.setColour(juce::Colour(kLcdOn));
					for(int y = 0; y < _snapshot.height; ++y)
						for(int x = 0; x < _snapshot.width; ++x)
							if(_snapshot.mono[static_cast<size_t>(y) * _snapshot.width + x])
								pixels.fillRect(x * kGraphicPixelPitch, y * kGraphicPixelPitch,
								                kGraphicPixelSize, kGraphicPixelSize);
				}
				m_canvas->setFixedTextureSize(imageWidth, imageHeight);
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
		void createGraphicBase(const int _width, const int _height)
		{
			m_graphicBase = juce::Image(juce::Image::ARGB, _width * kGraphicPixelPitch,
			                                 _height * kGraphicPixelPitch, true);
			juce::Graphics pixels(m_graphicBase);
			pixels.fillAll(juce::Colour(kLcdGlass));
		}

		static constexpr uint32_t kLcdGlass = 0xffff6f0fu;
		static constexpr uint32_t kLcdOffOverlay = 0x38000000u;
		static constexpr uint32_t kLcdOn = 0xff000000u;
		static constexpr int kGraphicPixelPitch = 4;
		static constexpr int kGraphicPixelSize = 4;

		juceRmlUi::ElemCanvas* m_canvas = nullptr;
		juce::Image m_source;
		juce::Image m_graphicBase;
		std::vector<uint32_t> m_pixels;
	};
}
