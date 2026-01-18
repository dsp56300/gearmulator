#pragma once

#include <map>
#include <memory>
#include <set>
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

		RmlPluginContext& m_context;
		Rml::ElementDocument* m_document = nullptr;
		Rml::Vector2f m_mouseDownPos;
		bool m_shiftDown = false;
		Rml::ObserverPtr<Rml::Element> m_shiftTargetSlider;
		int m_shiftDownParameterStartValue = 0;

		struct ControllerLinkDesc
		{
			Rml::Element* source;
			std::string target;
			std::string conditionButton;
		};

		std::vector<ControllerLinkDesc> m_controllerLinkDescs;
		std::vector<std::unique_ptr<ControllerLink>> m_controllerLinks;

		std::map<std::string, std::unique_ptr<TabGroup>> m_tabGroups;

		std::set<Rml::Element*> m_disabledSliders;

		std::unique_ptr<DocumentListener> m_documentListener;

		bool m_mouseIsDown = false;
	};
}
