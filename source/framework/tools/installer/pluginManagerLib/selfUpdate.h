#pragma once

#include <string>
#include <vector>

namespace pluginManager
{
	class Catalog;
	struct Asset;

	// Updating this program with itself.
	//
	// Its build is in the catalog like any other - TheUsualSuspects-PluginManager-<version>-<platform>.zip,
	// a program rather than a plugin and so a Standalone - which means the download, the unpacking
	// and the asking for rights are the same code every other install goes through. The one thing
	// that is different is that the file being replaced is the one that is running, and no system
	// lets that be overwritten. It can be moved aside, though, on all three, so that is what happens:
	// the new build is written where the old one was and the old one is deleted on the next start.
	//
	// Nothing here decides to update. It is offered and the user presses it, because a program that
	// replaces itself and restarts while somebody is halfway through installing a plugin is not a
	// kindness.
	class SelfUpdate
	{
	public:
		// The build to update to, when there is one newer than this program for this machine.
		// Nullptr when there is not - which is also the answer when the catalog never saw one,
		// since the builds sit behind the team login until this program is published.
		static const Asset* find(const Catalog& _catalog);

		// The build carrying exactly the version this program already is. Not an update by itself -
		// usually it IS this program - but a protected channel ships the next build under the number
		// the last one had, so it is the candidate whose date is worth looking at.
		static const Asset* findSameVersion(const Catalog& _catalog);

		// What the archive holds and what therefore has to be replaced: the .exe on Windows, the
		// binary on Linux, the .app bundle on macOS. Also the name to find it by inside the archive,
		// which is not the product name - the product is PluginManager and the file is called
		// "TUS Plugin Manager".
		static std::string appFile();
		static std::string appFileName();
		static std::string appFolder();

		// Whether this program can replace itself where it is, which it cannot when it was put
		// somewhere only an administrator may write. The install itself can ask for rights, but the
		// moving aside below cannot, so this is asked first and the user is told to move it rather
		// than left with a half-done update.
		static bool canUpdateInPlace(std::string& _error);

		// Moves the running program aside so the new one can take its place, and puts it back when
		// what came next did not work out. Without the undo a failed update leaves no program at all.
		static bool makeRoom(std::string& _error);
		static void restore();

		// Starts what is now in place and leaves it to the caller to quit this one. _parameters are
		// what this program was started with, passed on so that an update does not quietly drop
		// them - --settings points at another folder entirely, and coming back without it would
		// look like the logins and everything else had been lost.
		static bool restart(const std::string& _parameters, std::string& _error);

		// Whether this copy is the one an update just started. It has to be let past the check that
		// allows only one of this program at a time, because the copy that started it is still on
		// its way out - turned away there, an update ends with no program running at all.
		static bool isRestart(const std::vector<std::string>& _args);

		// Deletes what makeRoom left behind. Called at startup, where the program running is by
		// definition not the one that was moved aside.
		static void cleanUp();
	};
}
