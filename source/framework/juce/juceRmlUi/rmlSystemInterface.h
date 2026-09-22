#pragma once

#include <functional>
#include <set>

#include "RmlUi/Core/SystemInterface.h"

namespace juceRmlUi
{
	class SystemInterface : public Rml::SystemInterface
	{
	public:
		using LogEntry = std::pair<Rml::Log::Type, Rml::String>;
		using CursorChangedCallback = std::function<void(const Rml::String&)>;

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

		// Register a callback invoked whenever RmlUi requests a cursor change
		// (cursor: <name> in RCSS, or pointer-shape hints from drag/etc).
		// The callback receives the RmlUi cursor name; the consumer is
		// responsible for translating it to a platform cursor. Pass an empty
		// callback to detach.
		void setCursorChangedCallback(CursorChangedCallback _callback) { m_cursorChangedCallback = std::move(_callback); }

		// Drives RmlUi from a clock the caller owns instead of the wall clock. An offline render
		// produces its frames as fast as it can, so animations and transitions have to be told
		// which point in time each frame stands for, or they would all land on the same one.
		void setTimeOverride(const double _seconds) { m_timeOverride = _seconds; m_hasTimeOverride = true; }
		void clearTimeOverride() { m_hasTimeOverride = false; }

		// Whether RmlUi's progress reports - every font face it loads, every cursor it asks for -
		// reach the log. Warnings and errors always do. A command line tool that brings a UI up
		// only to render it turns this off, so its output is its own.
		void setVerboseLogging(const bool _verbose) { m_verboseLogging = _verbose; }

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

		CursorChangedCallback m_cursorChangedCallback;

		double m_timeOverride = 0.0;
		bool m_hasTimeOverride = false;
		bool m_verboseLogging = true;
	};
}
