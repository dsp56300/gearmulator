#pragma once

#include <set>

#include "RmlUi/Core/SystemInterface.h"

namespace juceRmlUi
{
	class SystemInterface : public Rml::SystemInterface
	{
	public:
		using LogEntry = std::pair<Rml::Log::Type, Rml::String>;

		explicit SystemInterface(Rml::CoreInstance& _coreInstance);
		SystemInterface(const SystemInterface&) = delete;
		SystemInterface(SystemInterface&&) = delete;
		~SystemInterface() override;

		SystemInterface& operator=(const SystemInterface&) = delete;
		SystemInterface& operator=(SystemInterface&&) = delete;

		double GetElapsedTime() override;
		int TranslateString(Rml::String& _translated, const Rml::String& _input) override;
		void JoinPath(Rml::String& _translatedPath, const Rml::String& _documentPath, const Rml::String& _path) override;
		bool LogMessage(Rml::Log::Type _type, const Rml::String& _message) override;
		void SetMouseCursor(const Rml::String& _cursorName) override;
		void SetClipboardText(const Rml::String& _text) override;
		void GetClipboardText(Rml::String& _text) override;
		void ActivateKeyboard(Rml::Vector2f _caretPosition, float _lineHeight) override;
		void DeactivateKeyboard() override;

		void beginLogRecording();
		void endLogRecording();

		std::vector<LogEntry> getRecordedLogEntries()
		{
			auto r = std::move(m_logEntries);
			m_logEntries.clear();
			return r;
		}

		static void filterLogEntries(std::vector<LogEntry>& _entries, const std::set<Rml::Log::Type>& _types);

		static std::string logTypeToString(Rml::Log::Type _type);

	private:
		Rml::CoreInstance& m_coreInstance;

		std::vector<LogEntry> m_logEntries;
		bool m_recordingLog = false;
	};
}
