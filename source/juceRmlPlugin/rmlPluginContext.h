#pragma once

#include <memory>
#include <vector>

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
		RmlPluginContext(Rml::Context* _context, pluginLib::Controller& _controller);
		RmlPluginContext(const RmlPluginContext&) = delete;
		RmlPluginContext(RmlPluginContext&&) = delete;
		~RmlPluginContext();
		RmlPluginContext& operator=(const RmlPluginContext&) = delete;
		RmlPluginContext& operator=(RmlPluginContext&&) = delete;

		Rml::Context* getContext() const { return m_context; }
		RmlParameterBinding& getParameterBinding() { return m_binding; }

		void addDocument(std::unique_ptr<RmlPluginDocument>&& _rmlPluginDocument);
		void removeDocument(const Rml::ElementDocument* _document);

		void elementCreated(Rml::Element* _element);

	private:
		Rml::Context* const m_context;
		RmlParameterBinding m_binding;

		std::vector<std::unique_ptr<RmlPluginDocument>> m_documents;
	};
}
