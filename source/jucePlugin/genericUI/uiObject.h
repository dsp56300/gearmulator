#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace juce
{
	class HyperlinkButton;
	class TextButton;
	class Label;
	class DrawableButton;
	class ComboBox;
	class LookAndFeel_V4;
	class Slider;
	class Component;
	class DynamicObject;
	class var;
}

namespace genericUI
{
	class ButtonStyle;
	class UiObjectStyle;
	class Editor;

	class UiObject
	{
	public:
		explicit UiObject(const juce::var& _json);
		~UiObject();

		void createJuceTree(Editor& _editor) const;
		void createChildObjects(Editor& _editor, juce::Component& _parent) const;

		void apply(Editor& _editor, juce::Component& _target) const;
		void apply(Editor& _editor, juce::Slider& _target);
		void apply(Editor& _editor, juce::ComboBox& _target);
		void apply(Editor& _editor, juce::DrawableButton& _target);
		void apply(Editor& _editor, juce::Label& _target);
		void apply(Editor& _editor, juce::TextButton& _target);
		void apply(Editor& _editor, juce::HyperlinkButton& _target);

		void collectVariants(std::set<std::string>& _dst, const std::string& _property) const;

		juce::Component* createJuceObject(Editor& _editor);

		int getPropertyInt(const std::string& _key, int _default = 0) const;
		std::string getProperty(const std::string& _key, const std::string& _default = std::string()) const;

	private:
		bool hasComponent(const std::string& _component) const;
		template<typename T> T* createJuceObject(Editor& _editor);
		template<typename T> T* createJuceObject(Editor& _editor, T* _object);

		bool parse(juce::DynamicObject* _obj);

		template<typename T> void bindParameter(const Editor& _editor, T& _target) const;

		template<typename Target, typename Style>
		void createStyle(Editor& _editor, Target& _target, Style* _style);

		std::string m_name;
		std::map<std::string, std::map<std::string, std::string>> m_components;
		std::vector<std::unique_ptr<UiObject>> m_children;

		std::vector<std::unique_ptr<juce::Component>> m_juceObjects;
		std::unique_ptr<UiObjectStyle> m_style;
	};

	inline bool UiObject::hasComponent(const std::string& _component) const
	{
		return m_components.find(_component) != m_components.end();
	}
}
