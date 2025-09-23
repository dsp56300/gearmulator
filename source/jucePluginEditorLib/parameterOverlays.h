#pragma once

#include <map>
#include <memory>

#include "parameterOverlay.h"

namespace Rml
{
	class Element;
}

namespace rmlPlugin
{
	class RmlParameterBinding;
}

namespace jucePluginEditorLib
{
	class Editor;

	class ParameterOverlays
	{
	public:
		explicit ParameterOverlays(Editor& _editor, rmlPlugin::RmlParameterBinding& _binding);
		~ParameterOverlays();

		bool registerComponent(Rml::Element* _component);

		Editor& getEditor() const { return m_editor; }

		void refreshAll() const;

	private:
		void onBind(pluginLib::Parameter* _param, Rml::Element* _elem);
		void onUnbind(pluginLib::Parameter* _param, Rml::Element* _elem);

		ParameterOverlay* getOverlay(const Rml::Element* _comp);

		Editor& m_editor;
		rmlPlugin::RmlParameterBinding& m_binding;

		std::map<const Rml::Element*, std::unique_ptr<ParameterOverlay>> m_overlays;

		size_t m_onBindListenerId;
		size_t m_onUnbindListenerId;
	};
}
