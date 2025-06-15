#include "rmlSystemInterface.h"

#include <cassert>

#include "dsp56300/source/dsp56kEmu/logging.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace juceRmlUi
{

	SystemInterface::~SystemInterface()
	= default;

	double SystemInterface::GetElapsedTime()
	{
		return Rml::SystemInterface::GetElapsedTime();
	}

	int SystemInterface::TranslateString(Rml::String& _translated, const Rml::String& _input)
	{
		return Rml::SystemInterface::TranslateString(_translated, _input);
	}

	void SystemInterface::JoinPath(Rml::String& _translatedPath, const Rml::String& _documentPath,
		const Rml::String& _path)
	{
		Rml::SystemInterface::JoinPath(_translatedPath, _documentPath, _path);
	}

	bool SystemInterface::LogMessage(const Rml::Log::Type _type, const Rml::String& _message)
	{
		switch (_type)
		{
		case Rml::Log::LT_ALWAYS:
			LOG("RML LOG [always]: " << _message.c_str());
			break;
		case Rml::Log::LT_ERROR:
			LOG("RML LOG [error]: " << _message.c_str());
			assert(false && "RML error");
			break;
		case Rml::Log::LT_ASSERT:
			LOG("RML LOG [assert]: " << _message.c_str());
			assert(false && "RML assert");
			break;
		case Rml::Log::LT_WARNING:
			LOG("RML LOG [warning]: " << _message.c_str());
			break;
		case Rml::Log::LT_INFO:
			LOG("RML LOG [info]: " << _message.c_str());
			break;
		case Rml::Log::LT_DEBUG:
			LOG("RML LOG [debug]: " << _message.c_str());
			break;
		case Rml::Log::LT_MAX:
			LOG("RML LOG [MAX]: " << _message.c_str());
			break;
		}

		if (m_recordingLog)
			m_logEntries.emplace_back(_type, _message);

		if (_type == Rml::Log::LT_ASSERT || _type == Rml::Log::LT_ERROR)
			return Rml::SystemInterface::LogMessage(_type, _message);
		return true;
	}

	void SystemInterface::SetMouseCursor(const Rml::String& _cursorName)
	{
		Rml::Log::Message(Rml::Log::LT_INFO, "SetMouseCursor %s", _cursorName.c_str());
		Rml::SystemInterface::SetMouseCursor(_cursorName);
	}

	void SystemInterface::SetClipboardText(const Rml::String& _text)
	{
		juce::SystemClipboard::copyTextToClipboard(_text.c_str());
	}

	void SystemInterface::GetClipboardText(Rml::String& _text)
	{
		_text = juce::SystemClipboard::getTextFromClipboard().toStdString();
	}

	void SystemInterface::ActivateKeyboard(const Rml::Vector2f _caretPosition, const float _lineHeight)
	{
		Rml::SystemInterface::ActivateKeyboard(_caretPosition, _lineHeight);
	}

	void SystemInterface::DeactivateKeyboard()
	{
		Rml::SystemInterface::DeactivateKeyboard();
	}

	void SystemInterface::beginLogRecording()
	{
		m_logEntries.clear();
		m_recordingLog = true;
	}

	void SystemInterface::endLogRecording()
	{
		m_recordingLog = false;
	}

	void SystemInterface::filterLogEntries(std::vector<LogEntry>& _entries, const std::set<Rml::Log::Type>& _types)
	{
		if (_types.empty())
			return;
		auto it = _entries.begin();
		while (it != _entries.end())
		{
			if (_types.find(it->first) == _types.end())
				it = _entries.erase(it);
			else
				++it;
		}
	}

	std::string SystemInterface::logTypeToString(const Rml::Log::Type _type)
	{
		switch (_type)
		{
		case Rml::Log::LT_ALWAYS: return "always";
		case Rml::Log::LT_ERROR: return "error";
		case Rml::Log::LT_ASSERT: return "assert";
		case Rml::Log::LT_WARNING: return "warning";
		case Rml::Log::LT_INFO: return "info";
		case Rml::Log::LT_DEBUG: return "debug";
		default: return "unknown";
		}
	}
}
