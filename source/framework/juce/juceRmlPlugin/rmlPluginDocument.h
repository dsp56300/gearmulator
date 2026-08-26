#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "RmlUi/Core/Event.h"

namespace Rml
{
	class ElementFormControlInput;
	class Context;
	class ElementDocument;
	class Element;
}

namespace rmlPlugin
{
	class DocumentListener;
	class RmlPluginContext;
	class TabGroup;
	class ControllerLink;

	class RmlPluginDocument
	{
	public:
		RmlPluginDocument(RmlPluginContext& _context);
		RmlPluginDocument(const RmlPluginDocument&) = delete;
		RmlPluginDocument(RmlPluginDocument&&) = delete;

		~RmlPluginDocument();

		RmlPluginDocument& operator=(const RmlPluginDocument&) = delete;
		RmlPluginDocument& operator=(RmlPluginDocument&&) = delete;

		void loadCompleted(Rml::ElementDocument* _doc);
		void elementCreated(Rml::Element* _element);

		RmlPluginContext& getPluginContext() const { return m_context; }
		Rml::Context* getContext() const;
		Rml::ElementDocument* getDocument() const { return m_document; }

		bool selectTabWithElement(const Rml::Element* _element) const;

		void processEvent(const Rml::Event& _event);

		bool addControllerLink(Rml::Element* _source, Rml::Element* _target, Rml::Element* _conditionButton);

		static void enableSliderDefaultMouseInputs(Rml::ElementFormControlInput* _input, bool _enabled);

	private:
		void setMouseIsDown(bool _isDown);
		void endSliderDrag();

		static float getModifierScale(const Rml::Event& _event, Rml::Element* _slider);

		RmlPluginContext& m_context;
		Rml::ElementDocument* m_document = nullptr;
		Rml::Vector2f m_mouseDownPos;
		Rml::ObserverPtr<Rml::Element> m_dragTargetSlider;
		int m_dragStartValue = 0;
		float m_lastMod = -1.0f;
		Rml::Vector2f m_lastMousePos;
		int m_modAnchorValue = 0;

		struct ControllerLinkDesc
		{
			Rml::Element* source;
			std::string target;
			std::string conditionButton;
		};

		std::vector<ControllerLinkDesc> m_controllerLinkDescs;
		std::vector<std::unique_ptr<ControllerLink>> m_controllerLinks;

		std::map<std::string, std::unique_ptr<TabGroup>> m_tabGroups;

		std::unique_ptr<DocumentListener> m_documentListener;

		bool m_mouseIsDown = false;
	};
}
