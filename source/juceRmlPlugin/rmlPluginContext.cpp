#include "rmlPluginContext.h"
#include "rmlPluginDocument.h"
#include "RmlUi/Core/ElementDocument.h"

namespace rmlPlugin
{
	RmlPluginContext::RmlPluginContext(Rml::Context* _context, pluginLib::Controller& _controller, juceRmlUi::RmlComponent& _component)
	: m_context(_context)
	, m_binding(_controller, _context, _component)
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

	void RmlPluginContext::elementCreated(Rml::Element* _element)
	{
		const auto* attribParam = _element->GetAttribute("param");

		if (!attribParam)
			return;

		// this will usually fail here because the document is not fully loaded
		// yet, we attempt to bind pending elements later again

		if (!m_binding.bind(*_element, attribParam->Get<Rml::String>(_element->GetCoreInstance())))
			m_pendingElementsToBind.insert(_element);
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
			auto* elem = *it;
			const auto param = elem->GetAttribute("param", std::string());
			if (param.empty())
			{
				// param no longer needed
				++it;
				continue;
			}

			if (m_binding.bind(*elem, param))
			{
				it = m_pendingElementsToBind.erase(it);
				continue;
			}

			assert(false && "RmlPluginContext::bindPendingElements: Binding failed");
			allBound = false;
			++it;
		}

		return allBound;
	}
}
