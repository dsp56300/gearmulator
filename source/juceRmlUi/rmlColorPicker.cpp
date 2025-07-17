#include "rmlColorPicker.h"

#include "rmlHelper.h"

#include "rmlElemCanvas.h"
#include "rmlEventListener.h"
#include "juceRmlPlugin/skinConverter/scHelper.h"

#include "juce_graphics/juce_graphics.h"

#include "RmlUi/Core/Elements/ElementFormControlInput.h"

namespace juceRmlUi
{
	namespace
	{
		juce::Colour toJuceColor(const Rml::Colourb& _color)
		{
			return juce::Colour::fromRGBA(_color.red, _color.green, _color.blue, _color.alpha);
		}

		void setColorText(Rml::Element* _elem, const Rml::Colourb& _color)
		{
			const auto colorString = "#" + toJuceColor(_color).toDisplayString(false).toStdString();
			_elem->SetInnerRML(colorString);
			_elem->SetProperty("background-color", colorString);
		}
	}

	ColorPicker::ColorPicker(Rml::Element* _root, const Rml::Colourb& _initialColor) : m_initialColor(_initialColor)
	{
		using namespace juceRmlUi::helper;

		m_channelGradients[0] = findChildT<ElemCanvas>(_root, "gradientR");
		m_channelGradients[1] = findChildT<ElemCanvas>(_root, "gradientG");
		m_channelGradients[2] = findChildT<ElemCanvas>(_root, "gradientB");

		m_channelSliders[0] = findChildT<Rml::ElementFormControlInput>(_root, "sliderR");
		m_channelSliders[1] = findChildT<Rml::ElementFormControlInput>(_root, "sliderG");
		m_channelSliders[2] = findChildT<Rml::ElementFormControlInput>(_root, "sliderB");

		m_channelTexts[0] = findChildT<Rml::ElementFormControlInput>(_root, "textR");
		m_channelTexts[1] = findChildT<Rml::ElementFormControlInput>(_root, "textG");
		m_channelTexts[2] = findChildT<Rml::ElementFormControlInput>(_root, "textB");

		m_textColorOld = findChild(_root, "textColorValueOld");
		m_textColorNew = findChild(_root, "textColorValueNew");

		m_hueGradient = findChildT<ElemCanvas>(_root, "gradientHue");
		m_hueSlider = findChildT<Rml::ElementFormControlInput>(_root, "sliderH");

		m_saturationGradient = findChildT<ElemCanvas>(_root, "gradientSaturation");
		m_saturationPointer = findChild(_root, "saturationPointer");

		for (size_t c=0; c<m_channelGradients.size(); ++c)
		{
			m_channelGradients[c]->setRepaintGraphicsCallback([this, c](const juce::Image& _image, juce::Graphics& _graphics) { paintChannelGradient(_image, _graphics, c); });

			EventListener::Add(m_channelSliders[c], Rml::EventId::Change, [this, c](Rml::Event&)
			{
				auto col = m_currentColor;
				col[c] = static_cast<uint8_t>(::atoi(m_channelSliders[c]->GetValue().c_str()));
				setColor(col);
			});
		}

		m_hueGradient->setRepaintGraphicsCallback([](const juce::Image& _image, juce::Graphics& _graphics) { paintHueGradient(_image, _graphics); });

		EventListener::Add(m_hueSlider, Rml::EventId::Change, [this](Rml::Event& _event)
		{
			const auto value = m_hueSlider->GetValue();
			float hue = static_cast<float>(::atoi(value.c_str())) / 255.0f;
			auto col = toJuceColor(m_currentColor);
			col = col.withHue(hue);
			setColor(toRmlColor(col));
		});

		m_saturationGradient->SetProperty(Rml::PropertyId::Drag, Rml::Property(Rml::Style::Drag::Drag));

		EventListener::Add(m_saturationGradient, Rml::EventId::Mousedown, [this](const Rml::Event& _event)
		{
			setColorBySaturationGradient(_event);
		});
		EventListener::Add(m_saturationGradient, Rml::EventId::Drag, [this](const Rml::Event& _event)
		{
			setColorBySaturationGradient(_event);
		});

		m_saturationGradient->setRepaintGraphicsCallback([this](const juce::Image& _image, const juce::Graphics& _graphics) { paintSaturationGradient(_image, _graphics); });

		setColorText(m_textColorOld, _initialColor);

		setColor(_initialColor);
	}

	void ColorPicker::paintChannelGradient(const juce::Image& _image, juce::Graphics& _graphics, const size_t _channel) const
	{
		auto colorStart = m_currentColor;
		auto colorEnd = m_currentColor;

		colorStart[_channel] = 0;
		colorEnd[_channel] = 255;

		_graphics.setGradientFill(juce::ColourGradient::horizontal(
			toJuceColor(colorStart),
			0.0f,
			toJuceColor(colorEnd),
			static_cast<float>(_image.getWidth() - 1)));

		_graphics.fillAll();
	}

	void ColorPicker::setColorBySaturationGradient(const Rml::Event& _event)
	{
		const auto pos = _event.GetTargetElement()->GetAbsoluteOffset(Rml::BoxArea::Content);
		const auto size = m_saturationGradient->GetBox().GetSize(Rml::BoxArea::Content);
		const auto mousePos = helper::getMousePos(_event);
		const auto x = mousePos.x - pos.x;
		const auto y = mousePos.y - pos.y;
		const auto s = std::clamp(x / static_cast<float>(size.x), 0.0f, 1.0f);
		const auto b = 1.0f - std::clamp(y / static_cast<float>(size.y), 0.0f, 1.0f);
		float h, ss, bb;
		toJuceColor(m_currentColor).getHSB(h, ss, bb);
		auto col = juce::Colour::fromHSV(h, s, b, 1.0f);
		setColor(helper::toRmlColor(col));
	}

	void ColorPicker::paintHueGradient(const juce::Image& _image, juce::Graphics& _graphics)
	{
		for (size_t y=0; y<_image.getHeight(); ++y)
		{
			const auto hue = static_cast<float>(y) / static_cast<float>(_image.getHeight());
			const auto color = juce::Colour::fromHSV(hue, 1.0f, 1.0f, 1.0f);
			_graphics.setColour(color);
			_graphics.drawLine(0, static_cast<float>(y), static_cast<float>(_image.getWidth()), static_cast<float>(y));
		}
	}

	void ColorPicker::paintSaturationGradient(const juce::Image& _image, const juce::Graphics&)
	{
		const juce::Colour col = toJuceColor(m_currentColor);
		float h,s,b;
		col.getHSB(h, s, b);

		const juce::Colour colorTR = juce::Colour::fromHSV(h, 1.0f, 1.0f, 1.0f);

		const Rml::Colourf colorTRf(colorTR.getFloatRed(), colorTR.getFloatGreen(), colorTR.getFloatBlue(), colorTR.getFloatAlpha());
		const Rml::Colourf colorTLf(1.0f, 1.0f, 1.0f, 1.0f);

		juce::Image::BitmapData bitmapData(_image, juce::Image::BitmapData::writeOnly);

		const auto xInv = 1.0f / static_cast<float>(_image.getWidth() - 1);
		const auto yInv = 1.0f / static_cast<float>(_image.getHeight() - 1);

		for (int y = 0; y <_image.getHeight(); ++y)
		{
			const auto ratioY = 255.0f * (1.0f - static_cast<float>(y) * yInv);

			for (int x = 0; x <_image.getWidth(); ++x)
			{
				const auto ratioX = static_cast<float>(x) * xInv;

				const auto cr = Rml::Math::Lerp(ratioX, colorTLf.red, colorTRf.red);
				const auto cg = Rml::Math::Lerp(ratioX, colorTLf.green, colorTRf.green);
				const auto cb = Rml::Math::Lerp(ratioX, colorTLf.blue, colorTRf.blue);

				auto* ptr = reinterpret_cast<juce::PixelARGB*>(bitmapData.getPixelPointer(x, y));

				ptr->setARGB(255, 
					static_cast<uint8_t>(cr * ratioY), 
					static_cast<uint8_t>(cg * ratioY), 
					static_cast<uint8_t>(cb * ratioY));
			}
		}
	}

	void ColorPicker::setColor(const Rml::Colourb& _color)
	{
		if (m_currentColor == _color)
			return;

		if (m_changingColor)
			return;

		m_changingColor = true;

		m_currentColor = _color;

		for (size_t c=0; c<m_channelTexts.size(); ++c)
		{
			const auto s = std::to_string(_color[c]);
			m_channelTexts[c]->SetValue(s);
			m_channelSliders[c]->SetValue(s);
			m_channelGradients[c]->repaint();
		}

		setColorText(m_textColorNew, _color);

		m_saturationGradient->repaint();

		float h,s,b;
		toJuceColor(_color).getHSB(h, s, b);

		m_hueSlider->SetValue(std::to_string(static_cast<int>(h * 255.0f)));

		const auto size = m_saturationGradient->GetBox().GetSize(Rml::BoxArea::Content);

		m_saturationPointer->SetProperty(Rml::PropertyId::Left, Rml::Property(size.x * s, Rml::Unit::PX));
		m_saturationPointer->SetProperty(Rml::PropertyId::Top, Rml::Property(size.y * (1.0f - b), Rml::Unit::PX));

		m_changingColor = false;
	}
}
