#pragma once

#include <array>

#include "RmlUi/Core/Colour.h"
#include "RmlUi/Core/Event.h"
#include "RmlUi/Core/Math.h"

namespace juce
{
	class Graphics;
	class Image;
}

namespace juceRmlUi
{
	class ElemCanvas;
}

namespace Rml
{
	class ElementFormControlInput;
}

namespace Rml
{
	class Element;
}

namespace juceRmlUi
{
	class ColorPicker
	{
	public:
		using CompletionCallback = std::function<void(bool, const Rml::Colourb&)>;
		ColorPicker(Rml::Element* _root, CompletionCallback&& _completionCallback, const Rml::Colourb& _initialColor = {255,255,255,255});
		~ColorPicker();

		static std::unique_ptr<ColorPicker> createFromTemplate(const std::string& _templateName, Rml::Element* _parent, CompletionCallback&& _completionCallback, const Rml::Colourb& _initialColor = {255,255,255,255});

	private:
		void paintChannelGradient(const juce::Image& _image, juce::Graphics& _graphics, size_t _channel) const;
		void setColorBySaturationGradient(const Rml::Event& _event);
		static void paintHueGradient(const juce::Image& _image, juce::Graphics& _graphics);
		void paintSaturationGradient(const juce::Image& _image, const juce::Graphics& _graphics) const;

		void setColor(const Rml::Colourb& _color);

		void close(bool _confirmNewColor) const;

		Rml::Element* m_root;
		Rml::Colourb m_initialColor;
		Rml::Colourb m_currentColor{0,0,0,0};
		bool m_changingColor = false;

		std::array<Rml::ElementFormControlInput*, 3> m_channelTexts{};
		std::array<Rml::ElementFormControlInput*, 3> m_channelSliders{};
		std::array<ElemCanvas*, 3> m_channelGradients{};

		Rml::Element* m_textColorOld{};
		Rml::Element* m_textColorNew{};

		ElemCanvas* m_hueGradient{};
		Rml::ElementFormControlInput* m_hueSlider{};
		ElemCanvas* m_saturationGradient{};
		Rml::Element* m_saturationPointer{};

		CompletionCallback m_completionCallback;
	};
}
