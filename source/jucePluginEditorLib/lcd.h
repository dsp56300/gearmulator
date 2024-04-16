#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib
{
	class Lcd : public juce::Button, juce::Timer
	{
	public:
		explicit Lcd(Component& _parent, uint32_t _width, uint32_t _height);
		~Lcd() override;

		void setText(const std::vector<uint8_t> &_text);
		void setCgRam(const std::array<uint8_t, 64> &_data);

	protected:
		void postConstruct();

	private:
		void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override {}
		void paint(juce::Graphics& _g) override;
		juce::Path createPath(uint8_t _character) const;
		void onClicked();
		void timerCallback() override;

		virtual bool getOverrideText(std::vector<std::vector<uint8_t>>& _lines) = 0;
		virtual const uint8_t* getCharacterData(uint8_t _character) const = 0;

	private:
		Component& m_parent;

		std::array<juce::Path, 256> m_characterPaths;

		const float m_scaleW;
		const float m_scaleH;

		const uint32_t m_width;
		const uint32_t m_height;
		std::vector<uint8_t> m_overrideText;
		std::vector<uint8_t> m_text;
		std::array<std::array<uint8_t, 8>, 8> m_cgData{{{0}}};
		uint32_t m_charBgColor = 0xff000000;
		uint32_t m_charColor = 0xff000000;
	};
}
