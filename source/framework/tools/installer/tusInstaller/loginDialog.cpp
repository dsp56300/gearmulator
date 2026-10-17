#include "loginDialog.h"

#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/StringUtilities.h"

#include <cctype>
#include "RmlUi/Core/Elements/ElementFormControlInput.h"

namespace pluginManager
{
	LoginDialog::LoginDialog(Rml::ElementDocument& _doc)
		: m_overlay(_doc.GetElementById("login-overlay"))
		, m_title(_doc.GetElementById("login-title"))
		, m_error(_doc.GetElementById("login-error"))
		, m_submitButton(_doc.GetElementById("login-submit"))
		, m_reveal(_doc.GetElementById("login-reveal"))
		, m_panel(_doc.GetElementById("login-panel"))
		, m_folder(dynamic_cast<Rml::ElementFormControlInput*>(_doc.GetElementById("login-folder")))
		, m_user(dynamic_cast<Rml::ElementFormControlInput*>(_doc.GetElementById("login-user")))
		, m_password(dynamic_cast<Rml::ElementFormControlInput*>(_doc.GetElementById("login-password")))
		, m_passwordShown(dynamic_cast<Rml::ElementFormControlInput*>(_doc.GetElementById("login-password-shown")))
	{
		if (m_submitButton)
			juceRmlUi::EventListener::Add(m_submitButton, Rml::EventId::Click, [this](Rml::Event&) { submit(); });

		if (m_reveal)
		{
			juceRmlUi::EventListener::Add(m_reveal, Rml::EventId::Click, [this](Rml::Event&)
			{
				setPasswordRevealed(!m_revealed);
			});
		}

		if (auto* cancel = _doc.GetElementById("login-cancel"))
			juceRmlUi::EventListener::Add(cancel, Rml::EventId::Click, [this](Rml::Event&) { close(); });

		// Enter submits from either field and Escape backs out, handled the way the in-place editor
		// handles them.
		for (auto* input : {m_folder, m_user, m_password, m_passwordShown})
		{
			if (!input)
				continue;

			juceRmlUi::EventListener::Add(input, Rml::EventId::Submit, [this](Rml::Event&) { submit(); });

			juceRmlUi::EventListener::Add(input, Rml::EventId::Change, [this](const Rml::Event& _event)
			{
				if (_event.GetParameter("linebreak", false))
					submit();
			});

			juceRmlUi::EventListener::Add(input, Rml::EventId::Keydown, [this](Rml::Event& _event)
			{
				if (juceRmlUi::helper::getKeyIdentifier(_event) != Rml::Input::KI_ESCAPE)
					return;

				_event.StopPropagation();
				close();
			});
		}
	}

	void LoginDialog::open(const std::string& _title, const bool _withFolder, SubmitCallback _onSubmit)
	{
		if (!m_overlay || !m_user || !m_password)
			return;

		m_onSubmit = std::move(_onSubmit);

		if (m_title)
			m_title->SetInnerRML(Rml::StringUtilities::EncodeRml(_title));

		if (m_panel)
			m_panel->SetClass("with-folder", _withFolder);

		if (m_folder)
			m_folder->SetValue({});

		m_user->SetValue({});
		m_password->SetValue({});
		m_passwordShown->SetValue({});

		showError({});
		setBusy(false);

		m_overlay->SetClass("open", true);

		// Hidden again every time the prompt opens - it is opened rather than reused, and a password
		// left showing from the last one is not what anybody asked for. After the panel is shown and
		// not before: changing the type builds a new text widget, and one built while the panel is
		// still hidden lays itself out against nothing and goes on drawing where it did.
		setPasswordRevealed(false);

		// The prompt was display:none until now, and a hidden element does not take focus.
		m_overlay->GetOwnerDocument()->UpdateDocument();

		(_withFolder && m_folder ? m_folder : m_user)->Focus(true);
	}

	// The folders we know of, by the name the plugin is known by. What goes into the field is the
	// folder rather than the name, because that is what the request is built from - and it stays
	// visible there, so what was chosen can still be corrected or replaced by hand.
	Rml::ElementFormControlInput* LoginDialog::password() const
	{
		return m_revealed && m_passwordShown ? m_passwordShown : m_password;
	}

	void LoginDialog::setPasswordRevealed(const bool _revealed)
	{
		if (!m_password || !m_passwordShown)
			return;

		auto* from = password();

		m_revealed = _revealed;

		auto* to = password();

		if (from != to)
			to->SetValue(from->GetValue());

		m_password->SetClass("hidden", _revealed);
		m_passwordShown->SetClass("hidden", !_revealed);

		if (m_reveal)
		{
			m_reveal->SetClass("checked", _revealed);
			m_reveal->SetAttribute("title", _revealed ? "Hide password" : "Show password");
		}

		// Typing carries on where it left off rather than in whichever field was last clicked.
		to->Focus();
	}

	void LoginDialog::close()
	{
		if (!m_overlay)
			return;

		m_onSubmit = nullptr;
		setBusy(false);

		// A password left in a field would still be readable from the DOM, and that is as true of
		// the one that is not on show.
		if (m_password)
			m_password->SetValue({});

		if (m_passwordShown)
			m_passwordShown->SetValue({});

		m_overlay->SetClass("open", false);
	}

	void LoginDialog::failed(const std::string& _message)
	{
		setBusy(false);
		showError(_message);

		if (auto* field = password())
			field->Focus(true);
	}

	bool LoginDialog::isOpen() const
	{
		return m_overlay && m_overlay->IsClassSet("open");
	}

	void LoginDialog::submit()
	{
		if (m_busy || !m_onSubmit || !m_user || !m_password)
			return;

		Credentials credentials;
		credentials.user = m_user->GetValue();
		credentials.password = password()->GetValue();

		std::string folder;

		if (m_panel && m_panel->IsClassSet("with-folder") && m_folder)
		{
			folder = m_folder->GetValue();

			// The folders on the server are the plugin name in lower case, which is what people will
			// type either way.
			for (auto& c : folder)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

			if (folder.empty())
			{
				showError("Enter the folder the login is for.");
				return;
			}
		}

		if (!credentials.valid())
		{
			showError("Enter both a user name and a password.");
			return;
		}

		showError({});
		setBusy(true);

		m_onSubmit(folder, credentials);
	}

	void LoginDialog::showError(const std::string& _message)
	{
		if (m_error)
			m_error->SetInnerRML(Rml::StringUtilities::EncodeRml(_message));
	}

	void LoginDialog::setBusy(const bool _busy)
	{
		m_busy = _busy;

		if (!m_submitButton)
			return;

		m_submitButton->SetClass("busy", _busy);
		m_submitButton->SetInnerRML(_busy ? "Checking..." : "Log in");
	}
}
