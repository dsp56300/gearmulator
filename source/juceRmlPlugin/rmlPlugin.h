#pragma once

#include "rmlParameterBinding.h"

#include "RmlUi/Core/Plugin.h"

namespace rmlPlugin
{
	class RmlPluginDocument;
}

namespace pluginLib
{
	class ParameterDescriptions;
	class Controller;
}

namespace rmlPlugin
{
	class RmlPluginContext;
	class TabGroup;
	class ControllerLink;

	class RmlPlugin : public Rml::Plugin
	{
	public:
		RmlPlugin(Rml::CoreInstance& _coreInstance, pluginLib::Controller& _controller);
		RmlPlugin(const RmlPlugin&) = delete;
		RmlPlugin(RmlPlugin&&) = delete;

		~RmlPlugin() override;

		RmlPlugin& operator=(const RmlPlugin&) = delete;
		RmlPlugin& operator=(RmlPlugin&&) = delete;

		void onContextCreate(Rml::Context* _context, juceRmlUi::RmlComponent& _component);

		// Rml::Plugin overrides
		void OnContextDestroy(Rml::Context* _context) override;
		void OnElementCreate(Rml::Element* _element) override;
		void OnDocumentOpen(Rml::Context* _context, const Rml::String& _documentPath) override;
		void OnDocumentLoad(Rml::ElementDocument* _document) override;
		void OnDocumentUnload(Rml::ElementDocument* _document) override;

		RmlParameterBinding* getParameterBinding(Rml::Context* _context);

		bool selectTabWithElement(const Rml::Element* _element);

		RmlPluginDocument* getPluginDocument(Rml::ElementDocument* _doc) const;

	private:

		Rml::CoreInstance& m_coreInstance;
		pluginLib::Controller& m_controller;

		std::map<Rml::Context*, std::unique_ptr<RmlPluginContext>> m_contexts;

		std::unique_ptr<RmlPluginDocument> m_documentBeingLoaded;
	};
}
