#pragma once

#include <string>

#include <juce_audio_processors/juce_audio_processors.h>

#include "uiObject.h"

#include "editorInterface.h"

namespace genericUI
{
	class Editor : public juce::Component
	{
	public:
		explicit Editor(EditorInterface& _interface);
		Editor(const Editor&) = delete;
		Editor(Editor&&) = delete;

		void create(const std::string& _jsonFilename);

		std::string exportToFolder(const std::string& _folder) const;

		juce::Drawable* getImageDrawable(const std::string& _texture);
		std::unique_ptr<juce::Drawable> createImageDrawable(const std::string& _texture);
		const juce::Font& getFont(const std::string& _fontFile);

		void registerComponent(const std::string& _name, juce::Component* _component);

		EditorInterface& getInterface() const { return m_interface; }

		const std::vector<juce::Component*>& findComponents(const std::string& _name, uint32_t _expectedCount = 0) const;

		template<typename T>
		void findComponents(std::vector<T*>& _dst, const std::string& _name, uint32_t _expectedCount = 0) const
		{
			const auto& res = findComponents(_name, _expectedCount);
			for (auto* s : res)
			{
				auto* t = dynamic_cast<T*>(s);
				if(t)
					_dst.push_back(t);
			}
		}

		juce::Component* findComponent(const std::string& _name, bool _mustExist = true) const;

		template<typename T>
		T* findComponentT(const std::string& _name, bool _mustExist = true) const
		{
			juce::Component* c = findComponent(_name, _mustExist);
			return dynamic_cast<T*>(c);
		}

		float getScale() const { return m_scale; }

	private:
		EditorInterface& m_interface;

		std::string m_jsonFilename;

		std::map<std::string, std::unique_ptr<juce::Drawable>> m_drawables;
		std::map<std::string, juce::Font> m_fonts;

		std::unique_ptr<UiObject> m_rootObject;

		std::map<std::string, std::vector<juce::Component*>> m_componentsByName;

		float m_scale = 1.0f;
	};
}
