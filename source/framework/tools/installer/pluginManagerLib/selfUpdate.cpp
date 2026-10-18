#include "selfUpdate.h"

#include "catalog.h"
#include "config.h"
#include "log.h"
#include "platformUtils.h"
#include "zipExtractor.h"

#include <algorithm>

#include "juce_core/juce_core.h"

namespace pluginManager
{
	namespace
	{
		// Beside the program rather than in a temp folder, because it has to be gone by the next
		// start and the next start only knows where the program is.
		juce::File oldFile()
		{
			return juce::File(juce::String(SelfUpdate::appFile()) + ".old");
		}

		// Said on the command line of the copy an update starts, and nowhere else.
		constexpr const char* g_restartArg = "--updated";

		juce::File thisApp()
		{
			// The bundle on macOS, not the binary several folders inside it: what the archive holds
			// and what a user would drag to the bin is the bundle.
			return juce::File::getSpecialLocation(juce::File::currentApplicationFile);
		}
	}

	const Asset* SelfUpdate::find(const Catalog& _catalog)
	{
		const auto os = PlatformUtils::getCurrentOS();
		const auto arch = PlatformUtils::getCurrentArch();

		// Newest first, so the head is the only one worth comparing.
		const auto versions = _catalog.getVersions(g_appProduct, "Standalone", os, arch);

		if (versions.empty())
			return nullptr;

		// Not "different from": a build older than what is running is what somebody rolling back
		// would ask for by hand, and is never offered as an update.
		if (compareVersions(versions.front(), g_appVersion) <= 0)
			return nullptr;

		return _catalog.find(g_appProduct, "Standalone", versions.front(), os, arch);
	}

	const Asset* SelfUpdate::findSameVersion(const Catalog& _catalog)
	{
		return _catalog.find(g_appProduct, "Standalone", g_appVersion,
		                     PlatformUtils::getCurrentOS(), PlatformUtils::getCurrentArch());
	}

	std::string SelfUpdate::appFile()
	{
		return thisApp().getFullPathName().toStdString();
	}

	std::string SelfUpdate::appFileName()
	{
		return thisApp().getFileName().toStdString();
	}

	std::string SelfUpdate::appFolder()
	{
		return thisApp().getParentDirectory().getFullPathName().toStdString();
	}

	bool SelfUpdate::canUpdateInPlace(std::string& _error)
	{
		const auto folder = appFolder();

		if (ZipExtractor::checkWriteAccess(folder, _error) == ZipExtractor::WriteAccess::Yes)
			return true;

		// The install could ask for rights; moving the running program aside cannot, and without
		// that the new build has nowhere to land. Saying so beats failing halfway.
		_error = std::string(g_appName) + " is installed in " + folder + ", which this program may "
			"not write to. Move it to a folder of your own to update it from here, or replace it by "
			"hand.";

		return false;
	}

	bool SelfUpdate::makeRoom(std::string& _error)
	{
		const auto app = thisApp();
		const auto old = oldFile();

		// A leftover from an update that never got its second start. It is not running now whatever
		// happened, so it goes.
		if (old.exists() && !old.deleteRecursively())
		{
			_error = "Could not remove " + old.getFullPathName().toStdString();
			return false;
		}

		// Renaming the running program is allowed everywhere this runs - the file keeps its
		// contents and the process keeps its handle on them - while overwriting it is allowed
		// nowhere.
		if (!app.moveFileTo(old))
		{
			_error = "Could not move " + app.getFullPathName().toStdString() + " out of the way";
			return false;
		}

		return true;
	}

	void SelfUpdate::restore()
	{
		const auto old = oldFile();
		const auto app = thisApp();

		if (!old.exists() || app.exists())
			return;

		if (!old.moveFileTo(app))
			Log::write("Could not put " + old.getFullPathName().toStdString() + " back");
	}

	bool SelfUpdate::restart(const std::string& _parameters, std::string& _error)
	{
		const auto app = thisApp();

		if (!app.exists())
		{
			_error = app.getFullPathName().toStdString() + " is not there";
			return false;
		}

		// Once, however often this program has updated itself: the copy started below is handed
		// this copy's command line, which already holds the flag when this copy was itself started
		// by an update.
		auto parameters = juce::String(_parameters).replace(g_restartArg, "").trim();

		parameters = (parameters + " " + g_restartArg).trim();

		if (!app.startAsProcess(parameters))
		{
			_error = "Could not start " + app.getFullPathName().toStdString();
			return false;
		}

		return true;
	}

	bool SelfUpdate::isRestart(const std::vector<std::string>& _args)
	{
		return std::find(_args.begin(), _args.end(), g_restartArg) != _args.end();
	}

	void SelfUpdate::cleanUp()
	{
		const auto old = oldFile();

		if (!old.exists())
			return;

		// Not worth a word to the user either way: it is the version they just replaced, and the
		// next update will try again.
		Log::write("Removing the previous version at " + old.getFullPathName().toStdString() +
			(old.deleteRecursively() ? ": ok" : ": failed"));
	}
}
