#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "condition.h"
#include "controllerlink.h"
#include "tabgroup.h"

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
		explicit UiObject(const juce::var& _json, bool _isTemplate = false);
		~UiObject();

		void createJuceTree(Editor& _editor);
		void createChildObjects(Editor& _editor, juce::Component& _parent) const;
		void createTabGroups(Editor& _editor);
		void createControllerLinks(Editor& _editor);
		void registerTemplates(Editor& _editor) const;

		void apply(Editor& _editor, juce::Component& _target);
		void apply(Editor& _editor, juce::Slider& _target);
		void apply(Editor& _editor, juce::ComboBox& _target);
		void apply(Editor& _editor, juce::DrawableButton& _target);
		void apply(Editor& _editor, juce::Label& _target);
		void apply(Editor& _editor, juce::TextButton& _target);
		void apply(Editor& _editor, juce::HyperlinkButton& _target);
		void apply(Editor& _editor, juce::TreeView& _target);
		void apply(Editor& _editor, juce::ListBox& _target);
		void apply(Editor& _editor, juce::TextEditor& _target);

		void collectVariants(std::set<std::string>& _dst, const std::string& _property) const;

		juce::Component* createJuceObject(Editor& _editor);

		int getPropertyInt(const std::string& _key, int _default = 0) const;
		float getPropertyFloat(const std::string& _key, float _default = 0.0f) const;
		std::string getProperty(const std::string& _key, const std::string& _default = std::string()) const;

		size_t getConditionCountRecursive() const;
		size_t getControllerLinkCountRecursive() const;

		void setCurrentPart(Editor& _editor, uint8_t _part);

		const auto& getName() const { return m_name; }

	private:
		bool hasComponent(const std::string& _component) const;
		template<typename T> T* createJuceObject(Editor& _editor);
		template<typename T> T* createJuceObject(Editor& _editor, T* _object);
		void createCondition(Editor& _editor, juce::Component& _target);

		bool parse(juce::DynamicObject* _obj);

		template<typename T> void bindParameter(const Editor& _editor, T& _target) const;

		void readProperties(juce::Component& _target);

		template<typename Target, typename Style>
		void createStyle(Editor& _editor, Target& _target, Style* _style);

		bool m_isTemplate;
		std::string m_name;
		std::map<std::string, std::map<std::string, std::string>> m_components;
		std::vector<std::unique_ptr<UiObject>> m_children;

		std::vector<std::shared_ptr<UiObject>> m_templates;

		std::vector<std::unique_ptr<juce::Component>> m_juceObjects;
		std::unique_ptr<UiObjectStyle> m_style;

		std::unique_ptr<Condition> m_condition;

		TabGroup m_tabGroup;
		std::vector<std::unique_ptr<ControllerLink>> m_controllerLinks;
	};

	inline bool UiObject::hasComponent(const std::string& _component) const
	{
		return m_components.find(_component) != m_components.end();
	}
}
