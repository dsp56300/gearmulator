#pragma once

#include <functional>
#include <map>
#include <memory>

#include "parameterOverlay.h"

#include "RmlUi/Core/EventListener.h"

namespace Rml
{
	class Element;
	class ElementDocument;
}

namespace rmlPlugin
{
	class RmlParameterBinding;
}

namespace jucePluginEditorLib
{
	class Editor;

	class ParameterOverlays : public Rml::EventListener
	{
	public:
		explicit ParameterOverlays(Editor& _editor, rmlPlugin::RmlParameterBinding& _binding);
		~ParameterOverlays() override;

		bool registerComponent(Rml::Element* _component);

		Editor& getEditor() const { return m_editor; }

		void refreshAll() const;
		void setMidiLearnMode(bool _active);

		ParameterOverlay* findOverlayForParameter(const pluginLib::Parameter* _param);
		void forEachOverlayForParameter(const pluginLib::Parameter* _param, const std::function<void(ParameterOverlay&)>& _func);
		void updateMidiLearnOverlays() const;
		void refreshMidiLearnOverlays() const;

	private:
		void ProcessEvent(Rml::Event& _event) override;

		void acquireDocument(const Rml::Element* _elem);

		void onBind(pluginLib::Parameter* _param, Rml::Element* _elem);
		void onUnbind(pluginLib::Parameter* _param, Rml::Element* _elem);

		ParameterOverlay* getOverlay(const Rml::Element* _comp);

		Editor& m_editor;
		rmlPlugin::RmlParameterBinding& m_binding;

		std::map<const Rml::Element*, std::unique_ptr<ParameterOverlay>> m_overlays;

		size_t m_onBindListenerId;
		size_t m_onUnbindListenerId;

		Rml::ElementDocument* m_document = nullptr;
	};
}
