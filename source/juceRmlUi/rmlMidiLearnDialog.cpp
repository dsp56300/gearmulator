#include "rmlMidiLearnDialog.h"

#include "rmlEventListener.h"
#include "rmlHelper.h"

#include "Core/Template.h"
#include "Core/TemplateCache.h"

#include "RmlUi/Core/CoreInstance.h"
#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/Factory.h"

#include "synthLib/midiTypes.h"

#include "juce_events/juce_events.h"

namespace juceRmlUi
{
	MidiLearnDialog::MidiLearnDialog(Rml::Element* _root, CompletionCallback&& _completionCallback, std::string _parameterName)
		: m_root(_root)
		, m_completionCallback(std::move(_completionCallback))
		, m_parameterName(std::move(_parameterName))
		, m_receivedEvent(synthLib::MidiEventSource::Physical)
	{
		m_statusText = helper::findChild(m_root, "statusText");
		m_parameterText = helper::findChild(m_root, "parameterText");

		if (m_parameterText)
			m_parameterText->SetInnerRML(m_parameterName);

		// Cancel button
		auto* buttonCancel = helper::findChild(m_root, "buttonCancel");
		if (buttonCancel)
		{
			EventListener::Add(buttonCancel, Rml::EventId::Click, [this](Rml::Event& _event)
			{
				_event.StopPropagation();

				juce::MessageManager::callAsync([this]
				{
					callCompletionCallback(false);
				});
			});
		}
	}

	MidiLearnDialog::~MidiLearnDialog()
	{
		if (m_root && m_root->GetParentNode())
			m_root->GetParentNode()->RemoveChild(m_root);
		m_root = nullptr;
	}

	std::unique_ptr<MidiLearnDialog> MidiLearnDialog::createFromTemplate(
		const std::string& _templateName,
		Rml::Element* _parent,
		CompletionCallback&& _completionCallback,
		const std::string& _parameterName)
	{
		if (!_parent)
			return nullptr;

		auto* t = _parent->GetCoreInstance().template_cache->GetTemplate(_templateName);

		if (!t)
		{
			Rml::Log::Message(_parent->GetCoreInstance(), Rml::Log::LT_ERROR, "MidiLearnDialog template '%s' not found", _templateName.c_str());
			return nullptr;
		}

		auto elem = _parent->GetOwnerDocument()->CreateElement("div");

		auto* parsedElem = t->ParseTemplate(elem.get());

		if (!parsedElem)
		{
			Rml::Log::Message(_parent->GetCoreInstance(), Rml::Log::LT_ERROR, "MidiLearnDialog template '%s' could not be parsed", _templateName.c_str());
			return nullptr;
		}

		auto* attachedElem = _parent->AppendChild(std::move(elem));

		attachedElem->SetProperty(Rml::PropertyId::ZIndex, Rml::Property(100, Rml::Unit::NUMBER));

		return std::make_unique<MidiLearnDialog>(attachedElem, std::move(_completionCallback), _parameterName);
	}

	void MidiLearnDialog::onMidiReceived(const synthLib::SMidiEvent& _event)
	{
		m_receivedEvent = _event;
		callCompletionCallback(true);
	}

	void MidiLearnDialog::onConflict(const std::string& _existingParamName, const synthLib::SMidiEvent& _event)
	{
		m_isConflict = true;
		m_receivedEvent = _event;

		// Update UI to show conflict
		if (m_statusText)
		{
			std::string conflictMsg = "This MIDI message is already mapped to:<br/><strong>" 
				+ _existingParamName + "</strong><br/><br/>Replace existing mapping?";
			m_statusText->SetInnerRML(conflictMsg);
		}

		// Add confirm button for conflict resolution
		auto* buttonContainer = helper::findChild(m_root, "buttonContainer");
		if (buttonContainer)
		{
			Rml::ElementPtr buttonConfirm = buttonContainer->GetOwnerDocument()->CreateElement("div");
			if (buttonConfirm)
			{
				buttonConfirm->SetAttribute("id", "buttonConfirm");
				buttonConfirm->SetAttribute("class", "button ml-button");
				buttonConfirm->SetInnerRML("Replace");
				
				auto* confirmPtr = buttonConfirm.get();
				buttonContainer->InsertBefore(std::move(buttonConfirm), buttonContainer->GetFirstChild());

				EventListener::Add(confirmPtr, Rml::EventId::Click, [this](Rml::Event& _event)
				{
					_event.StopPropagation();
					callCompletionCallback(true);
				});
			}

			// Update cancel button text
			auto* buttonCancel = helper::findChild(m_root, "buttonCancel");
			if (buttonCancel)
				buttonCancel->SetInnerRML("Keep Existing");
		}
	}

	void MidiLearnDialog::callCompletionCallback(const bool _confirmed) const
	{
		if (m_completionCallback)
			m_completionCallback(_confirmed, m_receivedEvent);
	}
}
