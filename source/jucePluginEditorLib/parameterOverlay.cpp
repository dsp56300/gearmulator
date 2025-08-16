#include "parameterOverlay.h"

#include "pluginEditor.h"

#include "jucePluginLib/parameter.h"

#include "RmlUi/Core/ElementDocument.h"

namespace jucePluginEditorLib
{
	namespace
	{
		std::string toString(const ParameterOverlay::Type _type)
		{
			switch(_type)
			{
				case ParameterOverlay::Type::Lock: return "lock";
				case ParameterOverlay::Type::Link: return "link";
				default: return {};
			}
		}
	}

	ParameterOverlay::ParameterOverlay(ParameterOverlays& _overlays, Rml::Element* _component) : m_overlays(_overlays), m_component(_component)
	{
	}

	ParameterOverlay::~ParameterOverlay()
	{
		setParameter(nullptr);
	}

	void ParameterOverlay::onBind(pluginLib::Parameter* _parameter, Rml::Element*)
	{
		setParameter(_parameter);
	}

	void ParameterOverlay::onUnbind(pluginLib::Parameter* _parameter, Rml::Element*)
	{
		setParameter(nullptr);
	}

	void ParameterOverlay::toggleOverlay(Type _type, const bool _enable, float _opacity/* = 1.0f*/)
	{
		if(_enable)
		{
			if (m_overlayElements.find(_type) != m_overlayElements.end())
				return;

			auto overlay = m_component->GetOwnerDocument()->CreateElement("div");

			overlay->SetAttribute("class", "tus-parameteroverlay tus-parameteroverlaytype-" + toString(_type));

			overlay->SetProperty(Rml::PropertyId::Opacity, Rml::Property(_opacity, Rml::Unit::NUMBER));

			m_overlayElements.insert({ _type, m_component->AppendChild(std::move(overlay)) });
		}
		else
		{
			auto it = m_overlayElements.find(_type);
			if(it == m_overlayElements.end())
				return;
			auto* overlay = it->second;
			m_overlayElements.erase(it);
			overlay->GetParentNode()->RemoveChild(overlay);
		}
	}

	void ParameterOverlay::updateOverlays()
	{
		if(m_component->GetParentNode() == nullptr)
			return;

		const auto isLocked = m_parameter != nullptr && m_parameter->isLocked();
		const auto isLinkSource = m_parameter != nullptr && (m_parameter->getLinkState() & pluginLib::Source);
		const auto isLinkTarget = m_parameter != nullptr && (m_parameter->getLinkState() & pluginLib::Target);

		const auto linkAlpha = isLinkSource ? 1.0f : 0.5f;

		toggleOverlay(Type::Lock, isLocked);
		toggleOverlay(Type::Link, isLinkSource || isLinkTarget, linkAlpha);
	}

	void ParameterOverlay::setParameter(pluginLib::Parameter* _parameter)
	{
		if(m_parameter == _parameter)
			return;

		if(m_parameter)
		{
			m_parameter->onLockedChanged.removeListener(m_parameterLockChangedListener);
			m_parameter->onLinkStateChanged.removeListener(m_parameterLinkChangedListener);
			m_parameterLockChangedListener = InvalidListenerId;
			m_parameterLinkChangedListener = InvalidListenerId;
		}

		m_parameter = _parameter;

		if(m_parameter)
		{
			m_parameterLockChangedListener = m_parameter->onLockedChanged.addListener([this](pluginLib::Parameter*, const bool&)
			{
				updateOverlays();
			});
			m_parameterLinkChangedListener = m_parameter->onLinkStateChanged.addListener([this](pluginLib::Parameter*, const pluginLib::ParameterLinkType&)
			{
				updateOverlays();
			});
		}

		updateOverlays();
	}
}
