#pragma once

#include <functional>
#include <memory>
#include <string>
#include <cstdint>
#include <vector>

namespace juce
{
	class Component;
	class FileChooser;
}

namespace genericUI
{
	// Every native window of the framework goes through here: message boxes and file dialogs. Headless (isHeadless())
	// none opens. Each one is recorded instead and stays open until answer() closes it, which runs its callback, so an
	// automated client (the MCP server) can drive them without windows on the desktop
	class MessageBox
	{
	public:
		enum class Icon : uint8_t
		{
		    None,
		    Question,
		    Warning,
		    Info,
		};

		enum class Result : uint8_t
		{
			Yes = 0,
			No = 1,
			Ok = Yes,
			Cancel = No
		};

		enum class Type : uint8_t
		{
			Ok,
			YesNo,
			OkCancel,
			File,
		};

		using Callback = std::function<void(Result)>;
		using FileCallback = std::function<void(const std::vector<std::string>& _files)>;	// none: cancelled

		struct FileDialogDesc
		{
			std::string title;
			std::string initialPath;
			std::string patterns;		// "*.syx,*.mid"
			int flags = 0;				// juce::FileBrowserComponent::FileChooserFlags
		};

		// A file dialog, open as long as this exists. Destroying it cancels it without its callback
		class FileDialog
		{
		public:
			~FileDialog();
			FileDialog(const FileDialog&) = delete;
			FileDialog& operator = (const FileDialog&) = delete;

		private:
			friend class MessageBox;
			FileDialog() = default;

			std::unique_ptr<juce::FileChooser> m_chooser;
			uint32_t m_id = 0;	// recorded, while headless
		};

		struct Dialog
		{
			uint32_t id = 0;
			Type type = Type::Ok;
			Icon icon = Icon::None;
			std::string header;			// a file dialog: its title
			std::string message;
			std::string button;			// the text of an Ok button that has its own
			FileDialogDesc file;
			bool open = true;
			std::string answer;			// what closed it: ok, yes, no, cancel, or the files, one per line
		};

		static void showYesNo(Icon _icon, const std::string& _header, const std::string& _message, Callback _callback);
		static void showOkCancel(Icon _icon, const std::string& _header, const std::string& _message, Callback _callback);
		static void showOk(Icon _icon, const std::string& _header, const std::string& _message, juce::Component* _associatedComponent = nullptr);
		static void showOk(Icon _icon, const std::string& _header, const std::string& _message, juce::Component* _associatedComponent, std::function<void()> _callback);
		static void showOk(Icon _icon, const std::string& _header, const std::string& _message, std::function<void()> _callback);

		// one button with a text of its own, the legal disclaimer for example
		static void showNotice(Icon _icon, const std::string& _header, const std::string& _message, const std::string& _button, std::function<void()> _callback);

		static std::unique_ptr<FileDialog> showFileDialog(const FileDialogDesc& _desc, FileCallback _callback);

		// no windows: the host cannot show any (isHeadlessHost()), the environment variable GEARMULATOR_HEADLESS_DIALOGS
		// is set or setHeadless(true) was called
		static bool isHeadless();
		static void setHeadless(bool _headless);

		// a host without desktop, or a build tool that loads the plugin to read its properties and crashes on a window
		static bool isHeadlessHost();

		// the dialogs recorded while headless: the open ones and the last ones closed
		static std::vector<Dialog> getDialogs();

		// closes an open dialog and runs its callback: a message box with a result, a file dialog with files (none =
		// cancel). False if there is no such open dialog or it is of the other kind
		static bool answer(uint32_t _id, Result _result);
		static bool answer(uint32_t _id, const std::vector<std::string>& _files);
	};
}
