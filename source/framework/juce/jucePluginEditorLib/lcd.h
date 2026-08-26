#pragma once

#include <cstdint>
#include <vector>
#include <array>

#include "juceRmlUi/rmlElemCanvas.h"

#include "juce_graphics/juce_graphics.h"

namespace Rml
{
	class Element;
}

namespace jucePluginEditorLib
{
	class Lcd : public juce::Timer
	{
	public:
		explicit Lcd(Rml::Element* _parent, uint32_t _numCharsX, uint32_t _numCharsY);
		virtual ~Lcd();

		void setText(const std::vector<uint8_t> &_text);
		void setCgRam(const std::array<uint8_t, 64> &_data);

		Rml::Element* getElement() const;

	private:
		void setSize(uint32_t _width, uint32_t _height);
		void paint(const juce::Image& _image, juce::Graphics& _g);
		juce::Path createPath(uint8_t _character) const;
		void onClicked();

		void repaint() const;

		virtual bool getOverrideText(std::vector<std::string>& _lines) { return false; }
		virtual bool getOverrideText(std::vector<std::vector<uint8_t>>& _lines);
		virtual const uint8_t* getCharacterData(uint8_t _character) const = 0;

		void timerCallback() override;

		std::array<juce::Path, 256> m_characterPaths;

		float m_scaleW = 0;
		float m_scaleH = 0;

		uint32_t m_numCharsX = 0;
		uint32_t m_numCharsY = 0;

		uint32_t m_width = 0;
		uint32_t m_height = 0;

		std::vector<uint8_t> m_overrideText;
		std::vector<uint8_t> m_text;

		std::array<std::array<uint8_t, 8>, 8> m_cgData{{{0}}};

		uint32_t m_charBgColor = 0xff000000;
		uint32_t m_charColor = 0xff000000;

		juceRmlUi::ElemCanvas* m_canvas;
	};
}
