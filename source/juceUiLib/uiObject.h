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
		explicit UiObject(UiObject* _parent, const juce::var& _json, bool _isTemplate = false);
		~UiObject();

		template<typename TComponent, typename TStyle>
		void applyT(Editor& _editor, TComponent& _target);

		void collectVariants(std::set<std::string>& _dst, const std::string& _property) const;

		int getPropertyInt(const std::string& _key, int _default = 0) const;
		float getPropertyFloat(const std::string& _key, float _default = 0.0f) const;
		std::string getProperty(const std::string& _key, const std::string& _default = std::string()) const;

		size_t getConditionCountRecursive() const;
		size_t getControllerLinkCountRecursive() const;

		void setCurrentPart(Editor& _editor, uint8_t _part);

		const auto& getName() const { return m_name; }

		void updateKeyValueConditions(const std::string& _key, const std::string& _value) const;
		bool hasComponent(const std::string& _component) const;

		const auto& getChildren() const { return m_children; }

		const auto& getTabGroup() const { return m_tabGroup; }

		const auto& getControllerLinks() const { return m_controllerLinks; }

		const auto& getCondition() const { return m_condition; }

		std::set<std::string> readConditionValues() const;

		const auto& getTemplates() const { return m_templates; }

		const std::map<std::string, std::string>& getComponentProperties() const;

	private:
		juce::DynamicObject& applyStyle(juce::DynamicObject& _obj, const std::string& _styleName);

		static bool copyPropertiesRecursive(juce::DynamicObject& _target, const juce::DynamicObject& _source);

		bool parse(juce::DynamicObject* _obj);

		template<typename T> void bindParameter(const Editor& _editor, T& _target) const;

		void readProperties(juce::Component& _target);

		template<typename Target, typename Style>
		void createStyle(Editor& _editor, Target& _target, Style* _style);

		bool m_isTemplate;
		std::string m_name;
		std::map<std::string, std::map<std::string, std::string>> m_components;
		std::vector<std::unique_ptr<UiObject>> m_children;
		UiObject* m_parent = nullptr;

		std::vector<std::shared_ptr<UiObject>> m_templates;

		std::vector<std::unique_ptr<juce::Component>> m_juceObjects;
		std::unique_ptr<UiObjectStyle> m_style;

		std::map<std::string, juce::var> m_styles;

		std::unique_ptr<Condition> m_condition;

		TabGroup m_tabGroup;
		std::vector<std::unique_ptr<ControllerLink>> m_controllerLinks;
	};
}
