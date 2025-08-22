#pragma once

#include <stdexcept>
#include <string>

#include "uiObject.h"

#include "editorInterface.h"

namespace juce
{
	class Font;
	class Drawable;
}

namespace genericUI
{
	class Editor
	{
	public:
		explicit Editor(EditorInterface& _interface);
		Editor(const Editor&) = delete;
		Editor(Editor&&) = delete;

		void create(const std::string& _jsonFilename);

		juce::Drawable* getImageDrawable(const std::string& _texture);
		const juce::Font& getFont(const std::string& _fontFile);

		float getScale() const { return m_scale; }

		virtual void setPerInstanceConfig(const std::vector<uint8_t>& _data) {}
		virtual void getPerInstanceConfig(std::vector<uint8_t>& _data) {}

		const UiObject& getRootObject() const { return *m_rootObject; }

		void initRootScale(const float _scale) { m_scale = _scale; }

	private:
		EditorInterface& m_interface;

		std::string m_jsonFilename;

		std::map<std::string, std::unique_ptr<juce::Drawable>> m_drawables;
		std::map<std::string, juce::Font> m_fonts;

		std::unique_ptr<UiObject> m_rootObject;

		float m_scale = 1.0f;
	};
}
