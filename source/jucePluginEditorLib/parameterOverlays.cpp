#include "parameterOverlays.h"

#include "juceRmlPlugin/rmlPlugin.h"

#include "RmlUi/Core/ElementDocument.h"

namespace jucePluginEditorLib
{
	ParameterOverlays::ParameterOverlays(Editor& _editor, rmlPlugin::RmlParameterBinding& _binding) : m_editor(_editor), m_binding(_binding)
	{
		m_onBindListenerId = m_binding.evBind.addListener([this](pluginLib::Parameter* _param, Rml::Element* _element)
		{
			onBind(_param, _element);
		});

		m_onUnbindListenerId = m_binding.evUnbind.addListener([this](pluginLib::Parameter* _param, Rml::Element* _element)
		{
			onUnbind(_param, _element);
		});
	}

	ParameterOverlays::~ParameterOverlays()
	{
		m_binding.evBind.removeListener(m_onBindListenerId);
		m_binding.evUnbind.removeListener(m_onUnbindListenerId);

		if (m_document)
			m_document->RemoveEventListener(Rml::EventId::Tabchange, this, true);
	}

	bool ParameterOverlays::registerComponent(Rml::Element* _component)
	{
		if(m_overlays.find(_component) != m_overlays.end())
			return false;

		acquireDocument(_component);

		m_overlays.insert({_component, std::make_unique<ParameterOverlay>(*this, _component)});

		return true;
	}

	void ParameterOverlays::refreshAll() const
	{
		for (const auto& overlay : m_overlays)
			overlay.second->refresh();
	}

	void ParameterOverlays::setMidiLearnMode(const bool _active)
	{
		// retry document acquisition in case it was unavailable during initial binding
		if (!m_document && !m_overlays.empty())
			acquireDocument(m_overlays.begin()->first);

		for (const auto& overlay : m_overlays)
			overlay.second->setMidiLearnMode(_active);
	}

	ParameterOverlay* ParameterOverlays::findOverlayForParameter(const pluginLib::Parameter* _param)
	{
		for (const auto& [elem, overlay] : m_overlays)
		{
			if (overlay->getParameter() == _param)
				return overlay.get();
		}
		return nullptr;
	}

	void ParameterOverlays::forEachOverlayForParameter(const pluginLib::Parameter* _param, const std::function<void(ParameterOverlay&)>& _func)
	{
		for (const auto& [elem, overlay] : m_overlays)
		{
			if (overlay->getParameter() == _param)
				_func(*overlay);
		}
	}

	void ParameterOverlays::updateMidiLearnOverlays() const
	{
		for (const auto& overlay : m_overlays)
			overlay.second->setMidiLearnMode(overlay.second->getParameter() != nullptr);
	}

	void ParameterOverlays::refreshMidiLearnOverlays() const
	{
		for (const auto& overlay : m_overlays)
			overlay.second->updateMidiLearnOverlay();
	}

	void ParameterOverlays::acquireDocument(const Rml::Element* _elem)
	{
		if (m_document)
			return;
		m_document = _elem->GetOwnerDocument();
		if (m_document)
			m_document->AddEventListener(Rml::EventId::Tabchange, this, true);
	}

	void ParameterOverlays::ProcessEvent(Rml::Event& /*_event*/)
	{
		if (m_document)
			m_document->UpdateDocument();
		refreshAll();
	}

	void ParameterOverlays::onBind(pluginLib::Parameter* _param, Rml::Element* _elem)
	{
		registerComponent(_elem);

		auto* o = getOverlay(_elem);
		if(!o)
			return;

		o->onBind(_param, _elem);
	}

	void ParameterOverlays::onUnbind(pluginLib::Parameter* _param, Rml::Element* _elem)
	{
		auto* o = getOverlay(_elem);

		if(!o)
			return;

		o->onUnbind(_param, _elem);
	}

	ParameterOverlay* ParameterOverlays::getOverlay(const Rml::Element* _comp)
	{
		const auto it = m_overlays.find(_comp);
		if(it == m_overlays.end())
			return nullptr;
		return it->second.get();
	}
}
