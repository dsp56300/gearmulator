#include "rmlPluginContext.h"
#include "rmlPluginDocument.h"
#include "RmlUi/Core/ElementDocument.h"

namespace rmlPlugin
{
	RmlPluginContext::RmlPluginContext(Rml::Context* _context, pluginLib::Controller& _controller) : m_context(_context), m_binding(_controller, _context)
	{
	}

	RmlPluginContext::~RmlPluginContext()
	{
		m_documents.clear();
	}

	void RmlPluginContext::addDocument(std::unique_ptr<RmlPluginDocument>&& _rmlPluginDocument)
	{
		m_documents.emplace_back(std::move(_rmlPluginDocument));
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

		m_binding.bind(*_element, attribParam->Get<Rml::String>(_element->GetCoreInstance()));
	}
}
