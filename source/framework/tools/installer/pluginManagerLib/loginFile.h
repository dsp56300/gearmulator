#pragma once

#include <string>
#include <vector>

#include "credentials.h"

namespace pluginManager
{
	// A login handed over as a file or as a line of text, instead of a folder name, a user and a
	// password copied out of a chat message by hand.
	//
	// Several of them, because somebody given a build to try is often given more than one, and
	// because handing over two files to paste twice is the thing this exists to avoid.
	struct SharedLogin
	{
		std::string slug;
		Credentials credentials;
	};

	class LoginFile
	{
	public:
		// Reads what a file or the clipboard holds.
		//
		// False for anything that is not one of ours, which is what lets a dropped file or a pasted
		// line be offered to this first and handled some other way when it is not. A file carrying
		// no login at all is a failure too: there would be nothing to confirm.
		static bool parse(const std::string& _text, std::vector<SharedLogin>& _logins);

		// One line, so that the same thing can be a file to send or a line to paste into a chat.
		static std::string write(const std::vector<SharedLogin>& _logins);

		// What the password is kept as inside one.
		//
		// Obfuscation, not encryption, and worth calling that: this program has to read it, so the
		// key is in the program, and anybody holding the program can get the password back out.
		//
		// What it does stop is the password being read at a glance - sitting in a chat, in a
		// screenshot, in a phone backup, or copied out of the file into a forum post. That is the
		// leak that actually happens, and it is worth the few lines.
		//
		// The folder it belongs to is part of the key, so a blob cannot be lifted out and put into a
		// file claiming a different folder: changing the slug makes the password decode to rubbish.
		static std::string hide(const std::string& _password, const std::string& _slug);
		static std::string reveal(const std::string& _hidden, const std::string& _slug);
	};
}
