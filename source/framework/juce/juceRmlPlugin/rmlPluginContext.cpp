#include "rmlPluginContext.h"
#include "rmlPluginDocument.h"

#include "Debugger/ElementDebugDocument.h"

#include "juceRmlUi/juceRmlComponent.h"

#include "RmlUi/Core/ElementDocument.h"

namespace rmlPlugin
{
	RmlPluginContext::RmlPluginContext(Rml::Context* _context, pluginLib::Controller& _controller, juceRmlUi::RmlComponent& _component)
	: m_context(_context)
	, m_binding(_controller, _context, _component)
	, m_onPreUpdate(_component.evPreUpdate, [this](juceRmlUi::RmlComponent*)
	{
		// Retry parameter bindings for elements created at runtime — by now
		// they are attached to the tree and their data-model scope resolves.
		// This sweep is not a choice: Rml::Plugin offers OnElementCreate and
		// OnElementDestroy and nothing in between, so there is no attach hook to
		// bind on. Costs nothing while the set is empty, which is the steady
		// state - an element that never binds is a control that never works, so
		// it announces itself rather than lingering silently.
		bindPendingElements();
	})
	{
	}

	RmlPluginContext::~RmlPluginContext()
	{
		m_documents.clear();
	}

	void RmlPluginContext::addDocument(std::unique_ptr<RmlPluginDocument>&& _rmlPluginDocument)
	{
		m_documents.emplace_back(std::move(_rmlPluginDocument));

		bindPendingElements();
	}

	void RmlPluginContext::removeDocument(const Rml::ElementDocument* _document)
	{
		if (dynamic_cast<const Rml::Debugger::ElementDebugDocument*>(_document))
			return; // we don't manage debug documents

		for (auto it = m_documents.begin(); it != m_documents.end(); ++it)
		{
			if (it->get()->getDocument() == _document)
			{
				m_documents.erase(it);
				return;
			}
		}
		RMLUI_ASSERT(false && "RmlPluginContext::removeDocument: Document not found");
	}

	void RmlPluginContext::elementCreated(Rml::Element* _element, const bool _documentLoading)
	{
		const auto* attribParam = _element->GetAttribute("param");

		if (!attribParam)
			return;

		// This usually fails at creation time: while a document is loading the
		// document isn't complete yet, and elements created at runtime (e.g.
		// via SetInnerRML) have no parent at all, so the data-model scope
		// cannot be resolved. Failed elements are retried — at document-load
		// completion and once per frame. Runtime-created elements additionally
		// still need the per-element document setup (slider drag overrides,
		// double-click reset, mouse wheel), which only ran automatically for
		// elements created during a document load.

		if (!m_binding.bind(*_element, attribParam->Get<Rml::String>(_element->GetCoreInstance())))
			m_pendingElementsToBind.emplace(_element, !_documentLoading);
	}

	void RmlPluginContext::elementDestroyed(Rml::Element* _element)
	{
		m_pendingElementsToBind.erase(_element);

		// release a live binding too: the element pointer is about to go
		// stale and RmlUi only detaches the change listener, it does not
		// know about the element maps of the binding or the overlays
		m_binding.elementDestroyed(_element);
	}

	bool RmlPluginContext::selectTabWithElement(const Rml::Element* _element) const
	{
		if (auto* doc = getDocument(_element))
			return doc->selectTabWithElement(_element);
		return false;
	}

	RmlPluginDocument* RmlPluginContext::getDocument(const Rml::Element* _element) const
	{
		return getPluginDocument(_element->GetOwnerDocument());
	}

	RmlPluginDocument* RmlPluginContext::getPluginDocument(const Rml::ElementDocument* _doc) const
	{
		for (const auto& doc : m_documents)
		{
			if (doc->getDocument() == _doc)
				return doc.get();
		}
		return nullptr;
	}

	bool RmlPluginContext::bindPendingElements()
	{
		bool allBound = true;

		for (auto it = m_pendingElementsToBind.begin(); it != m_pendingElementsToBind.end();)
		{
			auto* elem = it->first;
			const auto param = elem->GetAttribute("param", std::string());
			if (param.empty())
			{
				// param attribute removed — never bindable
				it = m_pendingElementsToBind.erase(it);
				continue;
			}

			if (m_binding.bind(*elem, param))
			{
				if (it->second)
				{
					// runtime-created element: run the per-element document
					// setup that load-time elements got at creation
					if (auto* doc = getPluginDocument(elem->GetOwnerDocument()))
						doc->elementCreated(elem);
				}
				it = m_pendingElementsToBind.erase(it);
				continue;
			}

			// not attached to a data-model scope (yet) — retried next frame,
			// removed on element destruction
			allBound = false;
			++it;
		}

		return allBound;
	}
}
