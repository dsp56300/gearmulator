#pragma once

#include "jucePluginLib/midiLearnMapping.h"

#include <functional>
#include <memory>
#include <string>

namespace Rml
{
	class Element;
}

namespace juceRmlUi
{
	class MidiLearnDialog
	{
	public:
		using CompletionCallback = std::function<void(bool, const pluginLib::MidiLearnMapping&)>;
		
		MidiLearnDialog(Rml::Element* _root, CompletionCallback&& _completionCallback, std::string _parameterName);
		~MidiLearnDialog();

		static std::unique_ptr<MidiLearnDialog> createFromTemplate(
			const std::string& _templateName,
			Rml::Element* _parent,
			CompletionCallback&& _completionCallback,
			const std::string& _parameterName);

		void onMidiReceived(const pluginLib::MidiLearnMapping& _mapping);
		void onConflict(const std::string& _existingParamName, const pluginLib::MidiLearnMapping& _mapping);
		void updateProgress(size_t _eventCount, size_t _requiredCount);

	private:
		void callCompletionCallback(bool _confirmed) const;

		Rml::Element* m_root;
		Rml::Element* m_statusText;
		Rml::Element* m_parameterText;
		CompletionCallback m_completionCallback;
		std::string m_parameterName;
		pluginLib::MidiLearnMapping m_receivedMapping;
		bool m_isConflict = false;
	};
}
