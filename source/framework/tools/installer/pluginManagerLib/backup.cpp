#include "backup.h"

#include "installPaths.h"
#include "log.h"
#include "platformUtils.h"
#include "zipExtractor.h"

#include "baseLib/filesystem.h"

// For callAsync and Thread::launch: the work goes to a worker and the answer comes back.
#include "juce_events/juce_events.h"

#include <algorithm>

namespace pluginManager
{
	namespace
	{
		// Sorts and reads back as the same order, and carries no character a file name may not
		// have on any of the three platforms.
		constexpr const char* g_timeFormat = "%Y-%m-%d_%H%M%S";

		// Between stored and smallest. A ROM is mostly incompressible and is the bulk of what is in
		// here, so the last three levels buy very little for the time they take.
		constexpr int g_compression = 6;

		juce::File dataFolderOf(const std::string& _product)
		{
			return juce::File(juce::String(PlatformUtils::dataFolder(_product)));
		}

		std::string productOf(const juce::File& _backup)
		{
			const auto name = _backup.getFileNameWithoutExtension();
			const auto split = name.indexOfChar('_');

			// No underscore means it is not one of ours, and the whole name is the best guess there
			// is. No product name has one in it, which is what makes the first one the separator.
			return (split > 0 ? name.substring(0, split) : name).toStdString();
		}
	}

	std::string Backup::folder()
	{
		// Read from the file on every call rather than handed in, so the headless program and the
		// window agree on it without either telling the other. It is a small file and none of
		// the callers is in a loop.
		return baseLib::filesystem::validatePath(InstallPaths().getBackupFolder());
	}

	juce::int64 Backup::sizeOf(const std::string& _product)
	{
		const auto source = dataFolderOf(_product);

		if (!source.isDirectory())
			return 0;

		juce::int64 total = 0;

		for (const auto& file : source.findChildFiles(juce::File::findFiles, true))
			total += file.getSize();

		return total;
	}

	bool Backup::create(const std::string& _product, std::string& _file, std::string& _error)
	{
		const auto source = dataFolderOf(_product);
		const auto files = source.isDirectory()
			? source.findChildFiles(juce::File::findFiles, true)
			: juce::Array<juce::File>();

		if (files.isEmpty())
		{
			_error = _product + " has nothing to back up yet - " +
				source.getFullPathName().toStdString() + " is empty or missing.";
			return false;
		}

		const auto dir = juce::File(juce::String(folder()));

		if (!dir.isDirectory() && !dir.createDirectory().wasOk())
		{
			_error = "Could not create " + dir.getFullPathName().toStdString() + ".";
			return false;
		}

		auto target = dir.getChildFile(juce::String(_product) + "_" +
			juce::Time::getCurrentTime().formatted(g_timeFormat) + ".zip");

		// Two backups in the same second would otherwise be one file, and the second would destroy
		// the first. That is not hypothetical: restoring takes a copy of the current state first, so
		// the pair happens whenever a restore follows a backup quickly - and the one it would have
		// eaten is the one being restored.
		if (target.exists())
			target = target.getNonexistentSibling();

		juce::ZipFile::Builder builder;

		for (const auto& file : files)
		{
			// Stored with forward slashes whatever wrote them, which is what the format says and
			// what makes a backup taken on one platform unpack on another.
			builder.addFile(file, g_compression,
				file.getRelativePathFrom(source).replaceCharacter('\\', '/'));
		}

		// Written beside the real name and moved into place, so that a run that dies halfway does
		// not leave something that looks like a backup and is not one.
		const auto temp = target.getSiblingFile(target.getFileName() + ".part");

		temp.deleteFile();

		{
			juce::FileOutputStream stream(temp);

			if (!stream.openedOk() || !builder.writeToStream(stream, nullptr))
			{
				temp.deleteFile();
				_error = "Could not write " + target.getFullPathName().toStdString() + ".";
				return false;
			}
		}

		if (!temp.moveFileTo(target))
		{
			temp.deleteFile();
			_error = "Could not write " + target.getFullPathName().toStdString() + ".";
			return false;
		}

		_file = target.getFullPathName().toStdString();

		Log::write("Backed up " + _product + " (" + std::to_string(files.size()) + " files) to " + _file);

		return true;
	}

	std::vector<Backup::Entry> Backup::list(const std::string& _product)
	{
		const auto dir = juce::File(juce::String(folder()));

		std::vector<Entry> entries;

		if (!dir.isDirectory())
			return entries;

		for (const auto& file : dir.findChildFiles(juce::File::findFiles, false, "*.zip"))
		{
			Entry e;

			e.product = productOf(file);

			if (!_product.empty() && e.product != _product)
				continue;

			e.file = file.getFullPathName().toStdString();
			e.size = file.getSize();

			// The file's own time rather than the one in its name. They agree for anything this
			// wrote, and parsing the name to cover a backup somebody copied about by hand is more
			// ways to be wrong than it is worth - the name is on screen either way.
			e.time = file.getLastModificationTime();

			entries.push_back(e);
		}

		std::sort(entries.begin(), entries.end(), [](const Entry& _a, const Entry& _b)
		{
			return _a.time > _b.time;
		});

		return entries;
	}

	bool Backup::restore(const std::string& _file, const std::string& _product, std::string& _error,
		const ProgressCallback& _onProgress)
	{
		const auto backup = juce::File(juce::String(_file));

		if (!backup.existsAsFile())
		{
			_error = backup.getFullPathName().toStdString() + " is not there any more.";
			return false;
		}

		// Before anything is written over. It fails when the folder is empty, which is the one case
		// where there is nothing to lose, so its failure is not this one's.
		std::string safety;
		std::string ignored;

		if (create(_product, safety, ignored))
			Log::write("Kept the current state of " + _product + " at " + safety);

		if (!ZipExtractor::extract(_file, PlatformUtils::dataFolder(_product), _error, _onProgress))
			return false;

		Log::write("Restored " + _product + " from " + _file);

		return true;
	}

	void Backup::createAsync(const std::string& _product, CompletionCallback _onComplete)
	{
		auto onComplete = std::make_shared<CompletionCallback>(std::move(_onComplete));

		juce::Thread::launch([_product, onComplete]
		{
			std::string file;
			std::string error;

			const auto success = create(_product, file, error);

			const auto message = success
				? "Backed up to " + juce::File(juce::String(file)).getFileName().toStdString()
				: error;

			juce::MessageManager::callAsync([onComplete, success, message]
			{
				if (*onComplete)
					(*onComplete)(success, message);
			});
		});
	}

	void Backup::restoreAsync(const std::string& _file, const std::string& _product,
		ProgressCallback _onProgress, CompletionCallback _onComplete)
	{
		auto onProgress = std::make_shared<ProgressCallback>(std::move(_onProgress));
		auto onComplete = std::make_shared<CompletionCallback>(std::move(_onComplete));

		juce::Thread::launch([_file, _product, onProgress, onComplete]
		{
			// Progress arrives on this thread, so it is handed to the message thread as well.
			ProgressCallback progress;

			if (*onProgress)
			{
				progress = [onProgress](const float _value)
				{
					juce::MessageManager::callAsync([onProgress, _value] { (*onProgress)(_value); });
				};
			}

			std::string error;

			const auto success = restore(_file, _product, error, progress);

			const auto message = success
				? "Restored from " + juce::File(juce::String(_file)).getFileName().toStdString()
				: error;

			juce::MessageManager::callAsync([onComplete, success, message]
			{
				if (*onComplete)
					(*onComplete)(success, message);
			});
		});
	}

	bool Backup::remove(const std::string& _file, std::string& _error)
	{
		const auto backup = juce::File(juce::String(_file));

		// Only out of our own folder. A path that came from somewhere else is not this program's to
		// delete, however it got into the list.
		if (!backup.isAChildOf(juce::File(juce::String(folder()))))
		{
			_error = backup.getFullPathName().toStdString() + " is not one of our backups.";
			return false;
		}

		if (!backup.existsAsFile() || backup.deleteFile())
			return true;

		_error = "Could not delete " + backup.getFullPathName().toStdString() + ".";

		return false;
	}
}
