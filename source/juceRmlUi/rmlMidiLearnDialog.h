#pragma once

#include <functional>
#include <memory>
#include <string>

#include "synthLib/midiTypes.h"

namespace Rml
{
	class Element;
}

namespace juceRmlUi
{
	class MidiLearnDialog
	{
	public:
		using CompletionCallback = std::function<void(bool, const synthLib::SMidiEvent&)>;
		
		MidiLearnDialog(Rml::Element* _root, CompletionCallback&& _completionCallback, std::string _parameterName);
		~MidiLearnDialog();

		static std::unique_ptr<MidiLearnDialog> createFromTemplate(
			const std::string& _templateName,
			Rml::Element* _parent,
			CompletionCallback&& _completionCallback,
			const std::string& _parameterName);

		void onMidiReceived(const synthLib::SMidiEvent& _event);
		void onConflict(const std::string& _existingParamName, const synthLib::SMidiEvent& _event);

	private:
		void callCompletionCallback(bool _confirmed) const;

		Rml::Element* m_root;
		Rml::Element* m_statusText;
		Rml::Element* m_parameterText;
		CompletionCallback m_completionCallback;
		std::string m_parameterName;
		synthLib::SMidiEvent m_receivedEvent;
		bool m_isConflict = false;
	};
}
