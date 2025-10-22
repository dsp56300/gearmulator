#include "parameterOverlays.h"

#include "juceRmlPlugin/rmlPlugin.h"

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
	}

	bool ParameterOverlays::registerComponent(Rml::Element* _component)
	{
		if(m_overlays.find(_component) != m_overlays.end())
			return false;

		m_overlays.insert({_component, std::make_unique<ParameterOverlay>(*this, _component)});

		return true;
	}

	void ParameterOverlays::refreshAll() const
	{
		for (const auto& overlay : m_overlays)
			overlay.second->refresh();
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
