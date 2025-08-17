#pragma once

#include "rmlParameterBinding.h"

#include "RmlUi/Core/Plugin.h"

namespace pluginLib
{
	class ParameterDescriptions;
	class Controller;
}

namespace rmlPlugin
{
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

		// Rml::Plugin overrides
		void OnContextCreate(Rml::Context* _context) override;
		void OnContextDestroy(Rml::Context* _context) override;
		void OnElementCreate(Rml::Element* _element) override;
		void OnDocumentLoad(Rml::ElementDocument* _document) override;

		RmlParameterBinding* getParameterBinding(Rml::Context* _context);

	private:
		void bindElementParameter(Rml::Element* _element);

		Rml::CoreInstance& m_coreInstance;
		pluginLib::Controller& m_controller;
		std::map<Rml::Context*, std::unique_ptr<RmlParameterBinding>> m_bindings;

		std::map<std::string, std::unique_ptr<TabGroup>> m_tabGroups;
		Rml::Context* m_lastCreatedContext = nullptr;

		struct ControllerLinkDesc
		{
			Rml::Element* source;
			std::string target;
			std::string conditionButton;
		};

		std::vector<ControllerLinkDesc> m_controllerLinkDescs;
		std::vector<std::unique_ptr<ControllerLink>> m_controllerLinks;
	};
}
