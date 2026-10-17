#pragma once

#include <functional>
#include <string>

#include "pluginManagerLib/credentials.h"

namespace Rml
{
	class Element;
	class ElementDocument;
	class ElementFormControlInput;
}

namespace pluginManager
{
	// Asks for the login of a channel that is not public.
	//
	// There is one prompt in the document. The team login and each product's donator login only
	// differ in the title and in what the caller does with the result: this class collects the
	// login, the caller checks it against the server and reports back with failed() or close().
	//
	// It touches the DOM directly. From a network callback, call it inside
	// RmlInterfaces::ScopedAccess; from an RmlUi event handler, call it as it is.
	class LoginDialog
	{
	public:
		// _folder is what the login is for, and is empty unless the prompt asked for one.
		using SubmitCallback = std::function<void(const std::string& _folder, const Credentials& _credentials)>;

		explicit LoginDialog(Rml::ElementDocument& _doc);

		// A donator login is for one folder on the server and is typed in. There was a button here
		// offering the plugins we know of, which is exactly the set this cannot be for: a plugin in
		// the catalog is a published one, and a published plugin has no donator builds to reach.
		void open(const std::string& _title, bool _withFolder, SubmitCallback _onSubmit);
		void close();

		// The login was rejected or could not be checked: keep the prompt open and say why.
		void failed(const std::string& _message);

		bool isOpen() const;

	private:
		void submit();

		// Swaps which of the two password fields is shown, carrying what was typed across.
		void setPasswordRevealed(bool _revealed);

		// Whichever of them is the one on show.
		Rml::ElementFormControlInput* password() const;
		void showError(const std::string& _message);
		void setBusy(bool _busy);

		Rml::Element* m_overlay = nullptr;
		Rml::Element* m_title = nullptr;
		Rml::Element* m_error = nullptr;
		Rml::Element* m_submitButton = nullptr;
		Rml::Element* m_reveal = nullptr;
		Rml::Element* m_panel = nullptr;
		Rml::ElementFormControlInput* m_folder = nullptr;
		Rml::ElementFormControlInput* m_user = nullptr;
		Rml::ElementFormControlInput* m_password = nullptr;
		Rml::ElementFormControlInput* m_passwordShown = nullptr;

		SubmitCallback m_onSubmit;
		bool m_revealed = false;

		// Set while the login is being checked, so a second Enter does not send a second request.
		bool m_busy = false;
	};
}
