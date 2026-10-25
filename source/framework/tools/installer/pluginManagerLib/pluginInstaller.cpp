#include "pluginInstaller.h"

#include "elevatedTask.h"
#include "httpClient.h"
#include "log.h"
#include "zipExtractor.h"

#include "juce_core/juce_core.h"
#include "juce_events/juce_events.h"

namespace pluginManager
{
	namespace
	{
		// A ROM the user kept inside the plugin, moved to where it belongs.
		//
		// The folder holding the binary is one of the two paths RomLoader always searches, and on
		// macOS that is inside the bundle, so people keep ROMs in there. That place is endangered by
		// every future update by design, because replacing the bundle is what an update is - so this
		// runs on an install as well as on an uninstall, and the fragile location gets drained the
		// first time the plugin is updated rather than merely survived.
		//
		// It cannot be done by asking what the install wrote: the manifest records the names in the
		// install folder, not what is inside a bundle, so for a bundle it holds one name - the
		// bundle. Nothing in there tells a user's file from the plugin's own. So the one kind of
		// file that cannot be downloaded again is taken out by what it is.
		//
		// .bin and .mid because those are the two extensions the ROM loaders search for, and no
		// build of ours ships either, so this can only ever find something somebody put there.
		// Documents/The Usual Suspects/<product>/roms/ because that is where the plugin looks
		// anyway, and nothing in this program ever deletes from it.
		void migrateRoms(const juce::File& _plugin, const std::string& _product)
		{
			// Somebody else's plugin under the same name: nothing in there was put there for ours.
			if (!_plugin.isDirectory() || !PlatformUtils::otherMaker(_plugin.getFullPathName().toStdString()).empty())
				return;

			const auto roms = juce::File(juce::String(PlatformUtils::romFolder(_product)));

			for (const auto& file : _plugin.findChildFiles(juce::File::findFiles, true, "*.bin;*.mid"))
			{
				if (!roms.isDirectory() && !roms.createDirectory().wasOk())
					return;

				auto dest = roms.getChildFile(file.getFileName());

				// The same file is already there, so this ran before and could not take the original
				// away - an install folder that needs rights does that. Without this check every
				// further update would add another copy under another name, and a roms folder filling
				// up with duplicates of one ROM is a plugin picking whichever it finds first.
				if (dest.existsAsFile() && dest.hasIdenticalContentTo(file))
				{
					file.deleteFile();
					continue;
				}

				// Something else of that name is there, and it is not this program's business to
				// decide which of two ROMs the user wanted. getNonexistentSibling keeps both.
				if (dest.exists())
					dest = dest.getNonexistentSibling();

				// Copy first and delete after, never moveFileTo: a move that fails halfway across a
				// volume boundary is the one outcome that must not happen to a ROM.
				if (!file.copyFileTo(dest))
				{
					Log::write("Could not keep " + file.getFullPathName().toStdString());
					continue;
				}

				// Taking the original away is what drains the bundle, but it is the lesser half: the
				// copy is already safe, and an install folder this user may not write to would fail
				// here. Then the file stays where it was and the install carries it on as before.
				const auto removed = file.deleteFile();

				Log::write("Moved " + file.getFullPathName().toStdString() + " to " +
					dest.getFullPathName().toStdString() + (removed ? "" : ", original kept"));
			}
		}

		// One thing an install put there, gone again. A plugin is a file in some formats and a folder
		// in others, so both are handled.
		bool removeOne(const juce::File& _file, std::string& _message, std::vector<std::string>& _needRights)
		{
			if (!_file.exists() || _file.deleteRecursively())
				return true;

			const auto path = _file.getFullPathName().toStdString();

			// What was installed with administrator rights needs them to be taken away again. That is
			// worth asking about; a plugin a host has open is not, and asking for rights that would
			// not help is worse than saying so - which is why the folder decides, not the failure.
			std::string accessError;

			const auto needsRights = ElevatedTask::isSupported() &&
				ZipExtractor::checkWriteAccess(_file.getParentDirectory().getFullPathName().toStdString(), accessError)
					== ZipExtractor::WriteAccess::NeedsRights;

			if (!needsRights)
			{
				_message = "Could not remove " + path +
					".\n\nIt may be open in a host, or belong to a user with more rights than this one.";
				return false;
			}

			// Not asked about here: a run that takes four formats of a product out of a system folder
			// would ask four times, and once more for every changelog beside them (EMU-221).
			_needRights.push_back(path);
			return true;
		}
	}

	bool PluginInstaller::run(const Asset& _asset, const Credentials& _credentials, const std::string& _targetDir,
	                          const std::string& _archiveFile,
	                          const ProgressCallback& _onProgress, const std::shared_ptr<bool>& _cancel,
	                          const State& _installing, const Files& _files, std::string& _message,
	                          const Pending& _pending)
	{
		if (_installing->exchange(true))
		{
			_message = "Already installing";
			return false;
		}

		struct Done
		{
			const State& installing;
			~Done() { *installing = false; }
		} done{_installing};

		if (_asset.url.empty())
		{
			_message = "No download available";
			return false;
		}

		if (_targetDir.empty())
		{
			_message = "There is no install location for " + (_asset.format.empty() ? std::string("this download") : _asset.format) +
				". Set one in Preferences.";
			return false;
		}

		// Checked first: there is no point downloading a build that cannot be unpacked afterwards.
		//
		// A plugin folder that only an administrator may write to is not one of those cases any
		// more. The download happens as this user either way, and the writing into place is what is
		// done with the rights for it - so the user is only asked once the build is here.
		const auto access = ZipExtractor::checkWriteAccess(_targetDir, _message);

		if (access == ZipExtractor::WriteAccess::No)
		{
			Log::write("Install " + _asset.product + " " + _asset.format + ": " + _message);
			return false;
		}

		const auto needsRights = access == ZipExtractor::WriteAccess::NeedsRights;

		if (needsRights && !ElevatedTask::isSupported())
			return false;

		// A name nothing else has: /tmp is shared, and a file left there under the same name by another
		// user cannot be replaced by this one.
		const auto tempZip = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getNonexistentChildFile(_asset.product + "_" + _asset.format + "_" + _asset.version, ".zip", false)
			.getFullPathName().toStdString();

		*_cancel = false;

		if (!HttpClient::downloadFile(_asset.url, tempZip, _message, _onProgress, _cancel, _credentials))
		{
			juce::File(tempZip).deleteFile();
			return false;
		}

		// What to look for in the archive: the Linux builds carry the plugin under a usr/local tree
		// of their own, and what is installed is the plugin rather than the tree. The caller may
		// name it, for a build whose file is not called after its product.
		const auto pluginFile = _archiveFile.empty()
			? PlatformUtils::pluginFileName(_asset.product, PlatformUtils::formatFromString(_asset.format))
			: _archiveFile;

		// Before the new build lands. The name in the archive is the name that gets replaced, so
		// that is the copy a ROM would be inside - and moving it out now is what keeps the next
		// update from being able to take it at all.
		migrateRoms(juce::File(juce::String(_targetDir)).getChildFile(juce::String(pluginFile)),
			_asset.getDataProduct());

		// What the archive leaves in the folder, so that taking it away again can take all of it.
		std::vector<std::string> files;

		// Deferred only where it would otherwise prompt: a folder this user may write to is written
		// to now, whatever else the run is collecting.
		ZipExtractor::PendingCopy pending;

		const auto extracted = ZipExtractor::install(tempZip, _targetDir, pluginFile, _asset.getDataProduct(),
			needsRights, files, _message,
			needsRights && _pending ? &pending : nullptr);

		// Promised rather than done, so what to write down afterwards goes with it. The install is
		// not recorded here: the files are not there yet, and a record of a version that is not on
		// disk is worse than no record at all.
		const auto deferred = extracted && !pending.job.targetDir.empty();

		if (deferred)
			_pending->add(std::move(pending), _asset, files);

		if (extracted && _files)
			*_files = std::move(files);

		juce::File(tempZip).deleteFile();

		if (extracted)
		{
			_message = _asset.product + " " + _asset.format + " " + _asset.version + " installed";
		}

		// Not "ok" for a copy that is only promised: whether it landed is up to the administrator
		// prompt still to come, and finishPending writes the line that says how that went.
		Log::write("Install " + _asset.product + " " + _asset.format + " " + _asset.version + " -> " + _targetDir +
			(deferred ? ": unpacked, waiting for administrator rights" : extracted ? ": ok" : ": " + _message));

		return extracted;
	}

	bool PluginInstaller::uninstall(const std::string& _product, const std::string& _dataProduct,
	                                const PluginFormat _format, const std::string& _dir,
	                                const std::vector<std::string>& _files, std::string& _message,
	                                std::vector<std::string>* _needRights, std::string* _plugin)
	{
		std::vector<std::string> ownNeedRights;
		auto& needRights = _needRights ? *_needRights : ownNeedRights;

		auto path = PlatformUtils::installedFilePath(_product, _format, _dir);

		if (path.empty())
		{
			_message = "There is no install location for this format.";
			return false;
		}

		// The name the product would have is a guess: a program keeps the name it was built under,
		// so OsirusTestConsole leaves virusTestConsole.exe behind. What the install wrote answers
		// instead, and with neither there is nothing of it here to remove.
		if (!juce::File(juce::String(path)).exists())
		{
			path.clear();

			for (const auto& candidate : _files)
			{
				if (!juce::File(juce::String(candidate)).exists())
					continue;

				path = candidate;
				break;
			}
		}

		if (path.empty())
		{
			_message = _product + " " + PlatformUtils::formatToString(_format) + " is not installed here.";
			return false;
		}

		// Found by its name alone, and the name is not ours alone.
		if (const auto maker = PlatformUtils::otherMaker(path); !maker.empty())
		{
			_message = path + " is a plugin of the same name by somebody else (" + maker + "), so it was left as it is.";
			return false;
		}

		const auto file = juce::File(juce::String(path));

		if (_plugin)
			*_plugin = path;

		// Before the delete, and whether or not the delete then works: a copy made twice is a
		// duplicate in a folder the user can see, and a copy not made at all is a ROM gone.
		migrateRoms(file, _dataProduct);

		// The plugin first: it is the one the answer is about, and the one worth stopping for. What
		// the same archive left beside it goes afterwards.
		if (!removeOne(file, _message, needRights))
			return false;

		for (const auto& other : _files)
		{
			if (other == path)
				continue;

			// Not worth failing the uninstall over: the plugin is gone, which is what was asked, and
			// a changelog nobody can delete is not a reason to say it is still installed.
			std::string ignored;
			removeOne(juce::File(juce::String(other)), ignored, needRights);
		}

		// Nobody to hand them to: ask once for all of this call's own, and let the plugin decide.
		if (!_needRights && !ownNeedRights.empty())
		{
			std::vector<bool> done;
			ElevatedTask::removeMany(ownNeedRights, done, _message);

			if (file.exists())
				return false;
		}

		_message = _product + " " + PlatformUtils::formatToString(_format) + " removed";

		return true;
	}

	bool PluginInstaller::install(const Asset& _asset, const Credentials& _credentials, const std::string& _targetDir,
	                              const ProgressCallback& _onProgress, std::string& _message,
	                              const std::string& _archiveFile, const Pending& _pending)
	{
		return run(_asset, _credentials, _targetDir, _archiveFile, _onProgress, m_cancel, m_installing,
			m_lastFiles, _message, _pending);
	}

	void PluginInstaller::installAsync(const Asset& _asset, const Credentials& _credentials, const std::string& _targetDir,
	                                   ProgressCallback _onProgress, CompletionCallback _onComplete,
	                                   const std::string& _archiveFile, const Pending& _pending)
	{
		auto onProgress = std::make_shared<ProgressCallback>(std::move(_onProgress));
		auto onComplete = std::make_shared<CompletionCallback>(std::move(_onComplete));

		juce::Thread::launch([_asset, _credentials, _targetDir, onProgress, onComplete, cancel = m_cancel,
		                      installing = m_installing, files = m_lastFiles, archiveFile = _archiveFile,
		                      pending = _pending]
		{
			// Progress arrives on this thread, so it is handed to the message thread as well.
			ProgressCallback progress;

			if (*onProgress)
			{
				progress = [onProgress](const float _progress)
				{
					juce::MessageManager::callAsync([onProgress, _progress] { (*onProgress)(_progress); });
				};
			}

			std::string message;

			const auto success = run(_asset, _credentials, _targetDir, archiveFile, progress, cancel,
				installing, files, message, pending);

			juce::MessageManager::callAsync([onComplete, success, message]
			{
				if (*onComplete)
					(*onComplete)(success, message);
			});
		});
	}
}
