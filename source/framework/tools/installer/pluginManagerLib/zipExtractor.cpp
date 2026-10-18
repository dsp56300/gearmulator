#include "zipExtractor.h"

#include "elevatedTask.h"
#include "log.h"
#include "platformUtils.h"

#include <utility>
#include <vector>

#include "juce_core/juce_core.h"

#if !JUCE_WINDOWS
#include <sys/stat.h>
#endif

namespace pluginManager
{
	namespace
	{
		// The two ways out are the two the user has, and which they are depends on whether this
		// system can ask for the rights at all.
		std::string waysOut()
		{
			return ElevatedTask::isSupported()
				? "Allow the install to run as administrator when asked, or pick a location you own in Preferences."
				: "Start the plugin manager as administrator, or pick a location you own in Preferences.";
		}

		std::string cannotWrite(const juce::File& _dir)
		{
			return "Cannot write to " + _dir.getFullPathName().toStdString() +
				".\n\nThis folder usually belongs to the system. " + waysOut();
		}

		// What the user kept inside a plugin that is being replaced, copied out to the folder the
		// plugin reads ROMs from.
		//
		// A plugin is a folder in some formats - every one of them on macOS, and LV2 everywhere - and
		// the folder its binary sits in is one of the two paths RomLoader searches, so a ROM put "next
		// to the plugin" can be inside the bundle. An update that deleted the old bundle deleted the
		// ROM with it, and the fix for that carried the ROM into the new bundle - which on macOS is
		// worse. A bundle there is sealed: one file in it that the signature does not cover makes
		// codesign report "a sealed resource is missing or invalid" and the system refuse to load it,
		// so the plugin simply stopped appearing in the host.
		//
		// So nothing is written into a bundle at all. What the user put in one is copied to
		// Documents/The Usual Suspects/<product>/roms/, the other path RomLoader searches, and the
		// bundle itself is replaced exactly as it was built and signed.
		//
		// Copied before the install rather than moved after it, for two reasons: the copy into place
		// may be made by the administrator half, which has no business writing into this user's
		// Documents folder, and a file that is only copied leaves the install folder exactly as it was
		// found if the install then cannot be made.
		void rescueUserFiles(const juce::File& _newBuild, const juce::File& _targetDir, const juce::File& _roms)
		{
			for (const auto& bundle : _newBuild.findChildFiles(juce::File::findDirectories, false))
			{
				const auto existing = _targetDir.getChildFile(bundle.getFileName());

				if (!existing.isDirectory())
					continue;

				for (const auto& file : existing.findChildFiles(juce::File::findFiles, true))
				{
					const auto relative = file.getRelativePathFrom(existing);

					// Part of the build, and the seal of the build that is going. Neither is the user's.
					if (bundle.getChildFile(relative).exists() || relative.contains("_CodeSignature"))
						continue;

					// Left in any folder the Finder has been asked to show, and worth nothing to anybody.
					if (file.getFileName() == ".DS_Store")
						continue;

					if (!_roms.isDirectory() && !_roms.createDirectory().wasOk())
					{
						Log::write("Could not make " + _roms.getFullPathName().toStdString());
						return;
					}

					auto dest = _roms.getChildFile(file.getFileName());

					// Already carried out of there by an earlier update of the same plugin.
					if (dest.existsAsFile() && dest.hasIdenticalContentTo(file))
						continue;

					// Something else of that name is there, and which of the two is wanted is not ours to say.
					if (dest.exists())
						dest = dest.getNonexistentSibling();

					if (file.copyFileTo(dest))
						Log::write("Kept " + file.getFullPathName().toStdString() + " as " + dest.getFullPathName().toStdString());
					else
						Log::write("Could not keep " + file.getFullPathName().toStdString());
				}
			}
		}

		// Where what is being replaced waits until the copy is through. The name ends in something
		// no host looks for, so an install folder never briefly holds a second plugin.
		juce::File asideFrom(const juce::File& _file)
		{
			for (int i = 0; i < 100; ++i)
			{
				const auto suffix = i ? juce::String(i) : juce::String();
				const auto candidate = _file.getSiblingFile(_file.getFileName() + ".tus-replaced" + suffix);

				if (!candidate.exists())
					return candidate;
			}

			return {};
		}

		// Whether what is already there can be written over. A plugin a host has loaded cannot be,
		// and that is the usual reason an install fails - so it is asked before anything is moved,
		// rather than found out halfway through.
		bool canReplace(const juce::File& _file, std::string& _error)
		{
			if (_file.isDirectory())
			{
				for (const auto& child : _file.findChildFiles(juce::File::findFiles, true))
				{
					if (!canReplace(child, _error))
						return false;
				}

				return true;
			}

			// Opening it for writing is the only thing that answers this: a lock is not something the
			// file says about itself.
			if (juce::FileOutputStream(_file).openedOk())
				return true;

			_error = "Could not replace " + _file.getFullPathName().toStdString() +
				".\n\nIt may be open in a host, or belong to a user with more rights than this one.";

			return false;
		}
	}

	ZipExtractor::WriteAccess ZipExtractor::checkWriteAccess(const std::string& _targetDir, std::string& _error)
	{
		if (_targetDir.empty())
		{
			_error = "No install location. Set one in Preferences.";
			return WriteAccess::No;
		}

		const auto dir = juce::File(juce::String(_targetDir));

		if (dir.existsAsFile())
		{
			_error = dir.getFullPathName().toStdString() + " is a file, not a folder.";
			return WriteAccess::No;
		}

		if (!dir.isDirectory() && !dir.createDirectory().wasOk())
		{
			// A folder under one that belongs to the system - the CLAP folder next to the VST3 one,
			// on a machine where nothing has made it yet. Creating it needs the same rights writing
			// in it would.
			_error = "Cannot create " + dir.getFullPathName().toStdString() + ".\n\n" + waysOut();

			return WriteAccess::NeedsRights;
		}

		// Windows reports a folder as writable that an unelevated process cannot write to, so the
		// only reliable check is to write something.
		const auto probe = dir.getChildFile(".tus-write-test");

		probe.deleteFile();

		if (!probe.create().wasOk())
		{
			_error = cannotWrite(dir);
			return WriteAccess::NeedsRights;
		}

		probe.deleteFile();

		return WriteAccess::Yes;
	}

	bool ZipExtractor::extract(const std::string& _zipPath, const std::string& _targetDir, std::string& _error,
		const ProgressCallback& _onProgress)
	{
		const auto zipFile = juce::File(juce::String(_zipPath));

		if (!zipFile.existsAsFile())
		{
			_error = "The downloaded archive is missing.";
			return false;
		}

		juce::ZipFile zip(zipFile);

		if (zip.getNumEntries() == 0)
		{
			_error = "The downloaded archive is empty or damaged.";
			return false;
		}

		if (!canWriteTo(_targetDir, _error))
			return false;

		const auto targetDir = juce::File(juce::String(_targetDir));

		// Entry by entry rather than uncompressTo, which is that same loop inside JUCE without a
		// way to say how far it has got. Restoring a product's folder is long enough to need one.
		const auto count = zip.getNumEntries();
		auto result = juce::Result::ok();

		for (int i = 0; i < count && result.wasOk(); ++i)
		{
			result = zip.uncompressEntry(i, targetDir, true);

			if (_onProgress)
				_onProgress(static_cast<float>(i + 1) / static_cast<float>(count));
		}

		if (result.wasOk())
		{
#if !JUCE_WINDOWS
			// JUCE reads the archive's unix mode only to tell a symlink from a file and then writes
			// every entry 0644, so the execute bit does not survive the unpacking. A plugin is loaded
			// rather than run and does not notice; a standalone build is a program the system has to
			// start, and the setup script beside it is one somebody may still want to run by hand.
			for (int i = 0; i < zip.getNumEntries(); ++i)
			{
				const auto* entry = zip.getEntry(i);

				// The mode sits in the high half of the external attributes, and is zero altogether in
				// an archive written where there is no such thing.
				if (!entry || !((entry->externalFileAttributes >> 16) & S_IXUSR))
					continue;

				targetDir.getChildFile(entry->filename).setExecutePermission(true);
			}
#endif
			return true;
		}

		// Unpacking can still fail on a folder that accepted the probe, for instance when one of the
		// files in it is in use, so whatever it says is passed on rather than swallowed.
		_error = "Could not unpack into " + targetDir.getFullPathName().toStdString() + ".\n\n" +
			result.getErrorMessage().toStdString();

		return false;
	}

	// Where the plugin actually sits in what came out of the archive. A build for Windows or macOS
	// holds it at the top, so this is the top; a Linux build holds it under usr/local/lib/<format>,
	// and then this is that folder - what goes into the install folder is what is AROUND the plugin,
	// not the prefix a package would have needed.
	juce::File ZipExtractor::pluginRoot(const juce::File& _unpacked, const std::string& _pluginFileName)
	{
		if (!_pluginFileName.empty())
		{
			if (_unpacked.getChildFile(juce::String(_pluginFileName)).exists())
				return _unpacked;

			const auto found = _unpacked.findChildFiles(juce::File::findFilesAndDirectories, true,
				juce::String(_pluginFileName));

			if (!found.isEmpty())
				return found.getFirst().getParentDirectory();
		}

		// Nothing to look for, or an archive that does not hold it. A plugin is named after its
		// product and is always found above; a program is not - OsirusTestConsole ships
		// virusTestConsole - so for those this is the only thing that strips the usr/local a Linux
		// build is packed under. Without it the whole tree lands in the install folder and the
		// program sits several folders inside it.
		auto root = _unpacked;

		for (;;)
		{
			// A folder holding a file is the build itself rather than something wrapped around it.
			if (!root.findChildFiles(juce::File::findFiles, false).isEmpty())
				return root;

			const auto dirs = root.findChildFiles(juce::File::findDirectories, false);

			if (dirs.size() != 1)
				return root;

			// A macOS bundle is a folder with a dot in its name and is what is being installed, not
			// a wrapper to step through. usr, local and bin have no dot in them.
			if (dirs.getFirst().getFileName().containsChar('.'))
				return root;

			root = dirs.getFirst();
		}
	}

#if !JUCE_WINDOWS
	namespace
	{
		bool isExecutable(const juce::File& _file)
		{
			struct stat s = {};

			return stat(_file.getFullPathName().toRawUTF8(), &s) == 0 && (s.st_mode & S_IXUSR) != 0;
		}

		// A copy does not necessarily carry the mode: macOS copies through NSFileManager, which
		// does, while Linux copies the bytes through a stream and the result is 0644 whatever the
		// source was. Rather than depend on which, the bit is put back from the staging copy,
		// where unpacking has just set it from the archive.
		void mirrorExecutePermission(const juce::File& _source, const juce::File& _target)
		{
			if (_source.isDirectory())
			{
				for (const auto& child : _source.findChildFiles(juce::File::findFilesAndDirectories, false))
					mirrorExecutePermission(child, _target.getChildFile(child.getFileName()));

				return;
			}

			if (isExecutable(_source))
				_target.setExecutePermission(true);
		}
	}
#endif

#if JUCE_MAC
	namespace
	{
		// macOS marks anything that came from the internet, and Gatekeeper then refuses to load a
		// plugin carrying the mark. A setup script shipped in every Mac archive to take it off
		// again, which the person installing by hand still needs - but installing through this
		// program is the moment it can be done for them, so it is.
		//
		// The same command the script runs, rather than removing com.apple.quarantine by name:
		// -c clears every attribute, which is what has been shipping and working, and a freshly
		// unpacked plugin carries nothing else worth keeping. It runs in whichever process did
		// the copying, so the rights are the ones that wrote the files.
		void clearExtendedAttributes(const juce::File& _file)
		{
			juce::ChildProcess xattr;

			if (xattr.start(juce::StringArray{"/usr/bin/xattr", "-cr", _file.getFullPathName()}))
				xattr.waitForProcessToFinish(10000);
		}
	}
#endif

	bool ZipExtractor::copyInto(const std::string& _sourceDir, const std::string& _targetDir, std::string& _error)
	{
		const auto source = juce::File(juce::String(_sourceDir));
		const auto target = juce::File(juce::String(_targetDir));

		if (!source.isDirectory())
		{
			_error = "There is nothing to install in " + _sourceDir + ".";
			return false;
		}

		if (!target.isDirectory() && !target.createDirectory().wasOk())
		{
			_error = "Could not create " + target.getFullPathName().toStdString() + ".";
			return false;
		}

		const auto entries = source.findChildFiles(juce::File::findFilesAndDirectories, false);

		// Asked before anything is moved. An install that cannot finish must not have started: a
		// bundle left half replaced is neither version, and no host can load it.
		for (const auto& entry : entries)
		{
			const auto existing = target.getChildFile(entry.getFileName());

			if (existing.exists() && !canReplace(existing, _error))
				return false;
		}

		// What was put there and what it replaced, in the order it happened, so that a copy which
		// does not finish anyway can be taken back.
		std::vector<std::pair<juce::File, juce::File>> done;

		const auto undo = [&done]
		{
			for (auto it = done.rbegin(); it != done.rend(); ++it)
			{
				it->first.deleteRecursively();

				if (it->second != juce::File())
					it->second.moveFileTo(it->first);
			}
		};

		for (const auto& entry : entries)
		{
			const auto placed = target.getChildFile(entry.getFileName());

			juce::File replaced;

			if (placed.exists())
			{
				replaced = asideFrom(placed);

				if (replaced == juce::File() || !placed.moveFileTo(replaced))
				{
					_error = "Could not move " + placed.getFullPathName().toStdString() + " out of the way.";
					undo();
					return false;
				}
			}

			// Recorded before the copy rather than after it, so that undo takes back the one that
			// failed halfway as well.
			done.emplace_back(placed, replaced);

			if (!(entry.isDirectory() ? entry.copyDirectoryTo(placed) : entry.copyFileTo(placed)))
			{
				_error = "Could not write " + placed.getFullPathName().toStdString() + ".";
				undo();
				return false;
			}
		}

		// All of it arrived, so what it replaced is not needed any more. Nothing of it is carried
		// into what replaced it - see rescueUserFiles, which takes what was the user's out of there
		// before any of this, and leaves the new bundle as it was built and signed.
		for (const auto& step : done)
		{
			if (step.second != juce::File())
				step.second.deleteRecursively();
		}

#if !JUCE_WINDOWS
		for (const auto& step : done)
			mirrorExecutePermission(source.getChildFile(step.first.getFileName()), step.first);
#endif

#if JUCE_MAC
		// After the copy rather than before it: what is cleared is what is now on the disk, and
		// a copy does not necessarily carry the attributes of what it was copied from.
		for (const auto& step : done)
			clearExtendedAttributes(step.first);
#endif

		return true;
	}

	// Unpacked first, put in place second. Two steps rather than one because the archive is not
	// necessarily shaped like the install folder - and because only the second step needs the rights
	// when the folder belongs to the system: unpacking an archive as administrator would run a good
	// deal of code with rights it does not need.
	bool ZipExtractor::install(const std::string& _zipPath, const std::string& _targetDir,
	                           const std::string& _pluginFileName, const std::string& _dataProduct,
	                           const bool _withRights,
	                           std::vector<std::string>& _files, std::string& _error,
	                           PendingCopy* _pending)
	{
		const auto unpacked = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("tus-install-" + juce::String(juce::Random::getSystemRandom().nextInt64()));

		// Whatever happens, the unpacked copy in the temp folder does not stay there - unless the copy
		// into place is still to come, and this is what it will be made from. Then it is left for
		// ElevatedBatch, which clears it once the copy has been made or given up on.
		struct Cleanup
		{
			juce::File dir;
			bool keep = false;
			~Cleanup() { if (!keep) dir.deleteRecursively(); }
		} cleanup{unpacked};

		if (!unpacked.createDirectory().wasOk())
		{
			_error = "Could not unpack the download in " + unpacked.getParentDirectory().getFullPathName().toStdString() + ".";
			return false;
		}

		if (!extract(_zipPath, unpacked.getFullPathName().toStdString(), _error))
			return false;

		const auto root = pluginRoot(unpacked, _pluginFileName);
		const auto source = root.getFullPathName().toStdString();

		// The changelog that rides along in the archive is for somebody who downloaded the archive.
		// A plugin folder is scanned by hosts and read by nobody, and ours was leaving a
		// changelog_<Product>.txt in it next to every build.
		//
		// Taken out of the staging folder rather than filtered at the copy, so that the copy, the
		// elevated copy and the list of what was installed all see the same thing. Only at the top
		// level: a .txt inside a bundle belongs to the plugin and is none of our business.
		juce::StringArray dropped;

		for (const auto& text : root.findChildFiles(juce::File::findFiles, false, "*.txt"))
		{
			dropped.add(text.getFileName());
			text.deleteFile();
		}

		// What is about to go in, taken from the staging folder rather than from the install folder:
		// the same list whether this copies it or the elevated half does, and it does not pick up
		// whatever was already there under another name.
		_files.clear();

		for (const auto& entry : root.findChildFiles(juce::File::findFilesAndDirectories, false))
			_files.push_back(entry.getFileName().toStdString());

		// A build packed without its program holds the changelog and nothing else. Copying nothing is
		// no install, and saying it worked left a row that never turned installed.
		if (_files.empty())
		{
			_error = "The download holds nothing to install.";
			return false;
		}

		// Before anything is written: a plugin of somebody else's that has the name of ours is not
		// ours to replace, and copying out of it is what filled a roms folder with its insides.
		for (const auto& name : _files)
		{
			const auto existing = juce::File(juce::String(_targetDir)).getChildFile(juce::String(name)).getFullPathName().toStdString();
			const auto maker = PlatformUtils::otherMaker(existing);

			if (maker.empty())
				continue;

			_error = existing + " is a plugin of the same name by somebody else (" + maker + "), so it was left as it is."
				"\n\nInstall this one into a folder of its own: the location field on its page takes one.";
			return false;
		}

		// Before anything is written, and as this user whichever way the copy is going to be made.
		rescueUserFiles(root, juce::File(juce::String(_targetDir)),
			juce::File(juce::String(PlatformUtils::romFolder(_dataProduct))));

		if (_withRights)
		{
			// Later, together with everything else in this run that needs the same rights.
			if (_pending)
			{
				*_pending = {{source, _targetDir}, unpacked.getFullPathName().toStdString()};
				cleanup.keep = true;

				return true;
			}

			return ElevatedTask::copyInto(source, _targetDir, _error);
		}

		if (!copyInto(source, _targetDir, _error))
			return false;

		// Now, and not before: the ones an earlier version of this program left in the install folder.
		// After the copy, because until it has been made the folder has to be exactly as it was found -
		// an install that cannot finish must not have started.
		for (const auto& name : dropped)
			juce::File(juce::String(_targetDir)).getChildFile(name).deleteFile();

		return true;
	}
}
