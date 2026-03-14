#include "parameterOverlay.h"

#include "parameterOverlays.h"
#include "pluginEditor.h"
#include "pluginProcessor.h"

#include "jucePluginLib/parameter.h"
#include "jucePluginLib/midiLearnPreset.h"
#include "jucePluginLib/midiLearnTranslator.h"

#include "juceRmlUi/rmlHelper.h"

#include "RmlUi/Core/ElementDocument.h"

#include <algorithm>

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
				case ParameterOverlay::Type::MidiLearn: return "midilearn";
				default: return {};
			}
		}

		std::string getMappingShortLabel(const pluginLib::MidiLearnMapping& _mapping)
		{
			switch (_mapping.type)
			{
			case pluginLib::MidiLearnMapping::Type::ControlChange:
				return "CC " + std::to_string(_mapping.controller);
			case pluginLib::MidiLearnMapping::Type::PitchBend:
				return "PB";
			case pluginLib::MidiLearnMapping::Type::ChannelPressure:
				return "AT";
			case pluginLib::MidiLearnMapping::Type::PolyPressure:
				return "PP " + std::to_string(_mapping.controller);
			case pluginLib::MidiLearnMapping::Type::NRPN:
				return "NRPN " + std::to_string(_mapping.nrpn);
			default:
				return {};
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

			auto* offsetParent = m_component->GetOffsetParent();

			if(!offsetParent)
				offsetParent = m_component->GetOwnerDocument();

			auto overlay = m_component->GetOwnerDocument()->CreateElement("div");

			overlay->SetAttribute("class", "tus-parameteroverlay tus-parameteroverlaytype-" + toString(_type));

			overlay->SetProperty(Rml::PropertyId::Opacity, Rml::Property(_opacity, Rml::Unit::NUMBER));
			overlay->SetProperty(Rml::PropertyId::Left, Rml::Property(m_component->GetOffsetLeft(), Rml::Unit::PX));
			overlay->SetProperty(Rml::PropertyId::Top, Rml::Property(m_component->GetOffsetTop(), Rml::Unit::PX));
			overlay->SetProperty(Rml::PropertyId::Width, Rml::Property(m_component->GetOffsetWidth(), Rml::Unit::PX));

			auto* elem = offsetParent->AppendChild(std::move(overlay));

			if (_type == Type::MidiLearn)
			{
				elem->AddEventListener(Rml::EventId::Click, this);
				elem->AddEventListener(Rml::EventId::Mousedown, this);
			}

			m_overlayElements.insert({ _type, elem });
		}
		else
		{
			auto it = m_overlayElements.find(_type);
			if(it == m_overlayElements.end())
				return;
			auto* overlay = it->second;

			if (_type == Type::MidiLearn)
			{
				overlay->RemoveEventListener(Rml::EventId::Click, this);
				overlay->RemoveEventListener(Rml::EventId::Mousedown, this);
			}

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

	void ParameterOverlay::ProcessEvent(Rml::Event& _event)
	{
		if (!m_midiLearnModeActive || !m_parameter)
			return;

		auto& editor = m_overlays.getEditor();

		if (_event.GetId() == Rml::EventId::Mousedown)
		{
			if (juceRmlUi::helper::isContextMenu(_event))
			{
				// right click: clear mapping for this parameter
				auto* translator = editor.getProcessor().getMidiLearnTranslator();
				if (!translator)
					return;

				auto preset = translator->getPreset();
				auto& mappings = preset.getMappings();
				const auto paramName = m_parameter->getDescription().name;

				mappings.erase(std::remove_if(mappings.begin(), mappings.end(),
					[&paramName](const pluginLib::MidiLearnMapping& _m)
					{
						return _m.paramName == paramName;
					}), mappings.end());

				translator->setPreset(preset);

				updateMidiLearnOverlay();
			}

			// block all mouse-down during MIDI learn mode to prevent parameter changes
			_event.StopImmediatePropagation();
		}
		else if (_event.GetId() == Rml::EventId::Click)
		{
			// left click: start learning for this parameter
			auto* translator = editor.getProcessor().getMidiLearnTranslator();
			if (!translator)
				return;

			// reset previous listening overlay
			if (auto* prevOverlay = m_overlays.findOverlayForParameter(editor.getMidiLearnSelectedParam()))
				prevOverlay->setMidiLearnListening(false);

			editor.setMidiLearnSelectedParam(m_parameter);
			translator->startLearning(m_parameter->getDescription().name);
			setMidiLearnListening(true);

			_event.StopImmediatePropagation();
		}
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

	void ParameterOverlay::setMidiLearnMode(const bool _active)
	{
		if (m_midiLearnModeActive == _active)
			return;
		m_midiLearnModeActive = _active;
		m_midiLearnListening = false;
		updateMidiLearnOverlay();
	}

	void ParameterOverlay::setMidiLearnListening(const bool _listening)
	{
		if (m_midiLearnListening == _listening)
			return;
		m_midiLearnListening = _listening;
		updateMidiLearnOverlay();
	}

	void ParameterOverlay::updateMidiLearnOverlay()
	{
		if (!m_midiLearnModeActive || !m_parameter)
		{
			toggleOverlay(Type::MidiLearn, false);
			return;
		}

		toggleOverlay(Type::MidiLearn, true);

		auto it = m_overlayElements.find(Type::MidiLearn);
		if (it == m_overlayElements.end())
			return;

		auto* elem = it->second;

		const auto label = getMidiLearnLabel();
		const bool isBound = !label.empty();

		elem->SetPseudoClass("midi-bound", isBound && !m_midiLearnListening);
		elem->SetPseudoClass("midi-unbound", !isBound && !m_midiLearnListening);
		elem->SetPseudoClass("midi-listening", m_midiLearnListening);

		// update or create label child
		if (elem->GetNumChildren() == 0)
		{
			auto span = m_component->GetOwnerDocument()->CreateElement("span");
			span->SetAttribute("class", "tus-midilearn-label");
			elem->AppendChild(std::move(span));
		}

		if (auto* labelElem = elem->GetChild(0))
			labelElem->SetInnerRML(m_midiLearnListening ? "..." : label);
	}

	std::string ParameterOverlay::getMidiLearnLabel() const
	{
		if (!m_parameter)
			return {};

		auto* translator = m_overlays.getEditor().getProcessor().getMidiLearnTranslator();
		if (!translator)
			return {};

		const auto& preset = translator->getPreset();
		const auto mappings = preset.findMappingsByParam(m_parameter->getDescription().name);

		if (mappings.empty())
			return {};

		return getMappingShortLabel(*mappings.front());
	}
}
