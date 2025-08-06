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

	class RmlPlugin : public Rml::Plugin
	{
	public:
		RmlPlugin(pluginLib::Controller& _controller);
		RmlPlugin(const RmlPlugin&) = delete;
		RmlPlugin(RmlPlugin&&) = delete;

		~RmlPlugin() override;

		RmlPlugin& operator=(const RmlPlugin&) = delete;
		RmlPlugin& operator=(RmlPlugin&&) = delete;

		// Rml::Plugin overrides
		void OnContextCreate(Rml::Context* _context) override;
		void OnContextDestroy(Rml::Context* _context) override;
		void OnElementCreate(Rml::Element* _element) override;

		RmlParameterBinding* getParameterBinding(Rml::Context* _context);

	private:
		pluginLib::Controller& m_controller;
		std::map<Rml::Context*, std::unique_ptr<RmlParameterBinding>> m_bindings;

		std::map<std::string, std::unique_ptr<TabGroup>> m_tabGroups;
	};
}
