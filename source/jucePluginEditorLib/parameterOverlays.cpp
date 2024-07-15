#include "parameterOverlays.h"

#include "jucePluginLib/parameterbinding.h"

namespace jucePluginEditorLib
{
	ParameterOverlays::ParameterOverlays(Editor& _editor, pluginLib::ParameterBinding& _binding) : m_editor(_editor), m_binding(_binding)
	{
		m_onBindListenerId = m_binding.onBind.addListener([this](const pluginLib::ParameterBinding::BoundParameter& _param)
		{
			onBind(_param);
		});

		m_onUnbindListenerId = m_binding.onUnbind.addListener([this](const pluginLib::ParameterBinding::BoundParameter& _param)
		{
			onUnbind(_param);
		});
	}

	ParameterOverlays::~ParameterOverlays()
	{
		m_binding.onBind.removeListener(m_onBindListenerId);
		m_binding.onUnbind.removeListener(m_onUnbindListenerId);
	}

	bool ParameterOverlays::registerComponent(juce::Component* _component)
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

	void ParameterOverlays::onBind(const pluginLib::ParameterBinding::BoundParameter& _parameter)
	{
		registerComponent(_parameter.component);

		auto* o = getOverlay(_parameter);
		if(!o)
			return;

		o->onBind(_parameter);
	}

	void ParameterOverlays::onUnbind(const pluginLib::ParameterBinding::BoundParameter& _parameter)
	{
		auto* o = getOverlay(_parameter);

		if(!o)
			return;

		o->onUnbind(_parameter);
	}

	ParameterOverlay* ParameterOverlays::getOverlay(const juce::Component* _comp)
	{
		const auto it = m_overlays.find(_comp);
		if(it == m_overlays.end())
			return nullptr;
		return it->second.get();
	}

	ParameterOverlay* ParameterOverlays::getOverlay(const pluginLib::ParameterBinding::BoundParameter& _parameter)
	{
		return getOverlay(_parameter.component);
	}
}
