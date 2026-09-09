#pragma once

#include <map>
#include <memory>
#include <vector>

#include "baseLib/event.h"

#include "rmlParameterBinding.h"

namespace pluginLib
{
	class Controller;
}

namespace Rml
{
	class Context;
}

namespace rmlPlugin
{
	class RmlPluginDocument;
}

namespace rmlPlugin
{
	class RmlPluginContext
	{
	public:
		RmlPluginContext(Rml::Context* _context, pluginLib::Controller& _controller, juceRmlUi::RmlComponent& _component);
		RmlPluginContext(const RmlPluginContext&) = delete;
		RmlPluginContext(RmlPluginContext&&) = delete;
		~RmlPluginContext();
		RmlPluginContext& operator=(const RmlPluginContext&) = delete;
		RmlPluginContext& operator=(RmlPluginContext&&) = delete;

		Rml::Context* getContext() const { return m_context; }
		RmlParameterBinding& getParameterBinding() { return m_binding; }

		void addDocument(std::unique_ptr<RmlPluginDocument>&& _rmlPluginDocument);
		void removeDocument(const Rml::ElementDocument* _document);

		void elementCreated(Rml::Element* _element, bool _documentLoading);
		void elementDestroyed(Rml::Element* _element);

		bool selectTabWithElement(const Rml::Element* _element) const;

		RmlPluginDocument* getDocument(const Rml::Element* _element) const;

		RmlPluginDocument* getPluginDocument(const Rml::ElementDocument* _doc) const;

		bool bindPendingElements();

	private:
		Rml::Context* const m_context;
		RmlParameterBinding m_binding;

		std::vector<std::unique_ptr<RmlPluginDocument>> m_documents;

		// Elements with a `param` attribute that could not be bound at creation
		// time. Elements created at runtime (e.g. SetInnerRML) have no parent
		// yet in OnElementCreate, so their data-model scope cannot be resolved;
		// they are retried once per frame (evPreUpdate) once attached. The value
		// is true when the element still needs the per-element document setup
		// (slider drag overrides etc.), which only runs automatically for
		// elements created while their document is loading.
		std::map<Rml::Element*, bool> m_pendingElementsToBind;

		baseLib::EventListener<juceRmlUi::RmlComponent*> m_onPreUpdate;
	};
}
