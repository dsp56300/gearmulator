#pragma once

#include <functional>
#include <map>
#include <memory>
#include <unordered_map>

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
		void setMidiLearnMode(bool _active);

		ParameterOverlay* findOverlayForParameter(const pluginLib::Parameter* _param);
		void forEachOverlayForParameter(const pluginLib::Parameter* _param, const std::function<void(ParameterOverlay&)>& _func);
		void updateMidiLearnOverlays() const;
		void refreshMidiLearnOverlays() const;

		// The lock/link/midi learn icons are created by a ParameterOverlay but live in the RmlUi
		// tree, where they can be destroyed behind its back - SetInnerRML on their parent does it.
		// Registering them here finds the owner when RmlUi reports that.
		void registerOverlayElement(const Rml::Element* _element, ParameterOverlay* _owner);
		void unregisterOverlayElement(const Rml::Element* _element);

	private:
		void onBind(pluginLib::Parameter* _param, Rml::Element* _elem);
		void onUnbind(pluginLib::Parameter* _param, Rml::Element* _elem);
		void onElementDestroyed(Rml::Element* _elem);

		ParameterOverlay* getOverlay(const Rml::Element* _comp);

		Editor& m_editor;
		rmlPlugin::RmlParameterBinding& m_binding;

		std::map<const Rml::Element*, std::unique_ptr<ParameterOverlay>> m_overlays;
		std::unordered_map<const Rml::Element*, ParameterOverlay*> m_overlayElementOwners;

		size_t m_onBindListenerId;
		size_t m_onUnbindListenerId;
		size_t m_onElementDestroyedListenerId;
	};
}
