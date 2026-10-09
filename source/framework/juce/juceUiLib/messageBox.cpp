#include "messageBox.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>

#include "juce_gui_basics/juce_gui_basics.h"
#include "juce_audio_processors/juce_audio_processors.h"

namespace genericUI
{
	namespace
	{
		juce::ModalComponentManager::Callback* addCallback(MessageBox::Callback _callback)
		{
			return juce::ModalCallbackFunction::create([callback = std::move(_callback)](const int _result)
			{
				callback(_result == 1 ? MessageBox::Result::Yes : MessageBox::Result::No);
			});
		}

		// juce::String's const char* constructor is ASCII-only by contract - it
		// does not decode anything above 127 - so a UTF-8 message handed to it
		// arrives as mojibake. Every string that reaches this file is UTF-8.
		juce::MessageBoxIconType toJuceIcon(const MessageBox::Icon _icon)
		{
			switch (_icon)
			{
				case MessageBox::Icon::None:     return juce::MessageBoxIconType::NoIcon;
				case MessageBox::Icon::Question: return juce::MessageBoxIconType::QuestionIcon;
				case MessageBox::Icon::Warning:  return juce::MessageBoxIconType::WarningIcon;
				case MessageBox::Icon::Info:     return juce::MessageBoxIconType::InfoIcon;
				default:                         return juce::MessageBoxIconType::NoIcon;
			}
		}

		juce::String toJuce(const std::string& _s)
		{
			return juce::String::fromUTF8(_s.c_str());
		}

		// the dialogs recorded while headless
		struct Recorded
		{
			MessageBox::Dialog dialog;
			MessageBox::Callback onResult;
			MessageBox::FileCallback onFiles;
		};

		constexpr size_t g_maxClosed = 100;

		struct Store
		{
			std::mutex mutex;
			std::deque<Recorded> dialogs;
			uint32_t nextId = 1;
			bool headless = false;
		};

		Store& store()
		{
			static Store s;
			return s;
		}

		uint32_t record(MessageBox::Dialog&& _dialog, MessageBox::Callback _onResult, MessageBox::FileCallback _onFiles)
		{
			auto& s = store();
			std::lock_guard lock(s.mutex);
			_dialog.id = s.nextId++;
			const auto id = _dialog.id;
			s.dialogs.push_back({std::move(_dialog), std::move(_onResult), std::move(_onFiles)});

			size_t closed = 0;
			for(const auto& d : s.dialogs)
				closed += d.dialog.open ? 0 : 1;
			for(auto it = s.dialogs.begin(); closed > g_maxClosed && it != s.dialogs.end();)
			{
				if(it->dialog.open)
				{
					++it;
					continue;
				}
				it = s.dialogs.erase(it);
				--closed;
			}
			return id;
		}

		void recordBox(const MessageBox::Type _type, const MessageBox::Icon _icon, const std::string& _header, const std::string& _message, MessageBox::Callback _callback, const std::string& _button = {})
		{
			MessageBox::Dialog d;
			d.type = _type;
			d.icon = _icon;
			d.header = _header;
			d.message = _message;
			d.button = _button;
			record(std::move(d), std::move(_callback), {});
		}

		MessageBox::Callback toResultCallback(std::function<void()> _callback)
		{
			return [callback = std::move(_callback)](MessageBox::Result)
			{
				if(callback)
					callback();
			};
		}

		const char* toString(const MessageBox::Result _result, const MessageBox::Type _type)
		{
			const bool yes = _result == MessageBox::Result::Yes;
			switch(_type)
			{
			case MessageBox::Type::YesNo:		return yes ? "yes" : "no";
			case MessageBox::Type::OkCancel:	return yes ? "ok" : "cancel";
			default:							return "ok";
			}
		}
	}

	MessageBox::FileDialog::~FileDialog()
	{
		if(!m_id)
			return;

		// the owner is gone, so is the callback's
		auto& s = store();
		std::lock_guard lock(s.mutex);
		for(auto& d : s.dialogs)
		{
			if(d.dialog.id != m_id || !d.dialog.open)
				continue;
			d.dialog.open = false;
			d.dialog.answer = "closed by its owner";
			d.onFiles = {};
		}
	}

	void MessageBox::showYesNo(const Icon _icon, const std::string& _header, const std::string& _message, Callback _callback)
	{
		if(isHeadless())
			return recordBox(Type::YesNo, _icon, _header, _message, std::move(_callback));

		juce::NativeMessageBox::showYesNoBox(toJuceIcon(_icon), toJuce(_header), toJuce(_message), nullptr, addCallback(std::move(_callback)));
	}

	void MessageBox::showOkCancel(const Icon _icon, const std::string& _header, const std::string& _message, Callback _callback)
	{
		if(isHeadless())
			return recordBox(Type::OkCancel, _icon, _header, _message, std::move(_callback));

		juce::NativeMessageBox::showOkCancelBox(toJuceIcon(_icon), toJuce(_header), toJuce(_message), nullptr, addCallback(std::move(_callback)));
	}

	void MessageBox::showOk(const Icon _icon, const std::string& _header, const std::string& _message, juce::Component* _associatedComponent/* = nullptr*/)
	{
		if(isHeadless())
			return recordBox(Type::Ok, _icon, _header, _message, {});

		juce::NativeMessageBox::showMessageBoxAsync(toJuceIcon(_icon), toJuce(_header), toJuce(_message), _associatedComponent);
	}

	void MessageBox::showOk(Icon _icon, const std::string& _header, const std::string& _message, juce::Component* _associatedComponent, std::function<void()> _callback)
	{
		if(isHeadless())
			return recordBox(Type::Ok, _icon, _header, _message, toResultCallback(std::move(_callback)));

		juce::NativeMessageBox::showMessageBoxAsync(toJuceIcon(_icon), toJuce(_header), toJuce(_message), _associatedComponent,
			juce::ModalCallbackFunction::create([_callback = std::move(_callback)](int)
			{
				_callback();
			}));
	}

	void MessageBox::showOk(Icon _icon, const std::string& _header, const std::string& _message, std::function<void()> _callback)
	{
		return showOk(_icon, _header, _message, nullptr, std::move(_callback));
	}

	void MessageBox::showNotice(const Icon _icon, const std::string& _header, const std::string& _message, const std::string& _button, std::function<void()> _callback)
	{
		if(isHeadless())
			return recordBox(Type::Ok, _icon, _header, _message, toResultCallback(std::move(_callback)), _button);

		const auto options = juce::MessageBoxOptions::makeOptionsOk(toJuceIcon(_icon), toJuce(_header), toJuce(_message), toJuce(_button));
		juce::NativeMessageBox::showAsync(options, [callback = std::move(_callback)](int)
		{
			if(callback)
				callback();
		});
	}

	std::unique_ptr<MessageBox::FileDialog> MessageBox::showFileDialog(const FileDialogDesc& _desc, FileCallback _callback)
	{
		std::unique_ptr<FileDialog> dialog(new FileDialog());

		if(isHeadless())
		{
			Dialog d;
			d.type = Type::File;
			d.header = _desc.title;
			d.file = _desc;
			dialog->m_id = record(std::move(d), {}, std::move(_callback));
			return dialog;
		}

		dialog->m_chooser = std::make_unique<juce::FileChooser>(toJuce(_desc.title),
			_desc.initialPath.empty() ? juce::File() : juce::File(toJuce(_desc.initialPath)), toJuce(_desc.patterns), true);

		dialog->m_chooser->launchAsync(_desc.flags, [callback = std::move(_callback)](const juce::FileChooser& _chooser)
		{
			std::vector<std::string> files;
			for(const auto& f : _chooser.getResults())
				files.push_back(f.getFullPathName().toStdString());
			callback(files);
		});
		return dialog;
	}

	bool MessageBox::isHeadless()
	{
		if(isHeadlessHost())
			return true;
		if(const auto* env = std::getenv("GEARMULATOR_HEADLESS_DIALOGS"); env && *env && *env != '0')
			return true;
		auto& s = store();
		std::lock_guard lock(s.mutex);
		return s.headless;
	}

	void MessageBox::setHeadless(const bool _headless)
	{
		auto& s = store();
		std::lock_guard lock(s.mutex);
		s.headless = _headless;
	}

	bool MessageBox::isHeadlessHost()
	{
		// returns false on a build machine without display even...
		if(juce::Desktop::getInstance().isHeadless())
			return true;

		const auto host = juce::PluginHostType::getHostPath();

		// So we use this instead. These tools cause crashes if you attempt to
		// open a message box. LV2 even opens the editor, even on a headless
		// build machine, whatever that is good for
		return host.contains("juce_vst3_helper") || host.contains("juce_lv2_helper");
	}

	std::vector<MessageBox::Dialog> MessageBox::getDialogs()
	{
		auto& s = store();
		std::lock_guard lock(s.mutex);
		std::vector<Dialog> result;
		for(const auto& d : s.dialogs)
			result.push_back(d.dialog);
		return result;
	}

	bool MessageBox::answer(const uint32_t _id, const Result _result)
	{
		Callback callback;
		{
			auto& s = store();
			std::lock_guard lock(s.mutex);
			auto it = std::find_if(s.dialogs.begin(), s.dialogs.end(), [&](const Recorded& _d) { return _d.dialog.id == _id; });
			if(it == s.dialogs.end() || !it->dialog.open || it->dialog.type == Type::File)
				return false;
			it->dialog.open = false;
			it->dialog.answer = toString(_result, it->dialog.type);
			callback = std::move(it->onResult);
		}
		if(callback)
			callback(_result);
		return true;
	}

	bool MessageBox::answer(const uint32_t _id, const std::vector<std::string>& _files)
	{
		FileCallback callback;
		{
			auto& s = store();
			std::lock_guard lock(s.mutex);
			auto it = std::find_if(s.dialogs.begin(), s.dialogs.end(), [&](const Recorded& _d) { return _d.dialog.id == _id; });
			if(it == s.dialogs.end() || !it->dialog.open || it->dialog.type != Type::File)
				return false;
			it->dialog.open = false;
			for(const auto& f : _files)
				it->dialog.answer += (it->dialog.answer.empty() ? "" : "\n") + f;
			if(_files.empty())
				it->dialog.answer = "cancel";
			callback = std::move(it->onFiles);
		}
		if(callback)
			callback(_files);
		return true;
	}
}
