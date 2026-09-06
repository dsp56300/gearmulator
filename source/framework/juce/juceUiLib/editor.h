#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace juce
{
	class Font;
	class Drawable;
}

namespace genericUI
{
	class UiObject;

	class Editor
	{
	public:
		explicit Editor();
		Editor(const Editor&) = delete;
		Editor(Editor&&) = delete;

		Editor& operator = (const Editor&) = delete;
		Editor& operator = (Editor&&) = delete;

		void create(const std::string& _jsonFilename);

		juce::Drawable* getImageDrawable(const std::string& _texture);
		const juce::Font& getFont(const std::string& _fontFile);

		float getScale() const { return m_scale; }

		virtual void setPerInstanceConfig(const std::vector<uint8_t>& _data) {}
		virtual void getPerInstanceConfig(std::vector<uint8_t>& _data) {}

		const UiObject& getRootObject() const { return *m_rootObject; }

		void initRootScale(const float _scale) { m_scale = _scale; }

		virtual const char* getResourceByFilename(const std::string& _name, uint32_t& _dataSize) = 0;

	private:
		std::string m_jsonFilename;

		std::map<std::string, std::unique_ptr<juce::Drawable>> m_drawables;
		std::map<std::string, juce::Font> m_fonts;

		std::unique_ptr<UiObject> m_rootObject;

		float m_scale = 1.0f;
	};
}
