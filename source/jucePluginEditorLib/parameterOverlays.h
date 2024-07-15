#pragma once

#include <map>
#include <memory>

#include "parameterOverlay.h"

#include "jucePluginLib/parameterbinding.h"

namespace pluginLib
{
	class ParameterBinding;
}

namespace juce
{
	class Component;
}

namespace jucePluginEditorLib
{
	class Editor;

	class ParameterOverlays
	{
	public:
		explicit ParameterOverlays(Editor& _editor, pluginLib::ParameterBinding& _binding);
		~ParameterOverlays();

		bool registerComponent(juce::Component* _component);

		Editor& getEditor() const { return m_editor; }

		void refreshAll() const;

	private:
		void onBind(const pluginLib::ParameterBinding::BoundParameter& _parameter);
		void onUnbind(const pluginLib::ParameterBinding::BoundParameter& _parameter);

		ParameterOverlay* getOverlay(const juce::Component* _comp);
		ParameterOverlay* getOverlay(const pluginLib::ParameterBinding::BoundParameter& _parameter);

		Editor& m_editor;
		pluginLib::ParameterBinding& m_binding;

		std::map<const juce::Component*, std::unique_ptr<ParameterOverlay>> m_overlays;
		size_t m_onBindListenerId;
		size_t m_onUnbindListenerId;
	};
}
