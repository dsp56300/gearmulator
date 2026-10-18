#pragma once

#include "elevatedTask.h"

#include <functional>
#include <string>
#include <vector>

#include "juce_core/juce_core.h"

namespace pluginManager
{
	class ZipExtractor
	{
	public:
		// 0 to 1, called from whichever thread is doing the unpacking.
		using ProgressCallback = std::function<void(float _progress)>;

		// Why a location cannot be written to, since the answer decides what happens next: a folder
		// that belongs to the system is one the user can be asked about, and a path that is a file
		// is not.
		enum class WriteAccess
		{
			Yes,
			NeedsRights,	// a system folder: writable by an administrator, and by nobody else
			No				// nothing anybody may write to, whatever rights they have
		};

		// Extracts into the target directory, creating it if needed. _error says what went wrong,
		// because the usual failure - a plugin folder that only an administrator may write to - is
		// something the user has to be told about rather than a plain "failed".
		static bool extract(const std::string& _zipPath, const std::string& _targetDir, std::string& _error,
			const ProgressCallback& _onProgress = {});

		// Unpacks a build into the folder its format is installed in.
		//
		// The archive goes into a folder of its own first, and what is copied into the install folder
		// is the folder the plugin turned out to be in: a Linux build carries a whole usr/local/lib/
		// tree around it, where the Windows and macOS ones hold the plugin at the top. Unpacked at
		// face value, a Linux plugin lands several folders deep and nothing ever finds it again.
		//
		// _pluginFileName is what to look for - an empty one takes the archive as it comes.
		// _dataProduct is the product whose roms folder takes what a user kept inside the bundle that is
		// replaced - see Asset::dataProduct.
		// _withRights does the last step through ElevatedTask, for a folder that belongs to the
		// system. Nothing is written into the install folder until the archive is out in one piece.
		// _files comes back holding the names this put into the install folder - the plugin and
		// whatever else the archive carried beside it. Written down by the caller, because an
		// uninstall that only knows the plugin's own name leaves the rest there for good.
		// _pending turns the last step into a promise: with rights needed and a list given, the build
		// is unpacked and the copy into place is added to it rather than made. A run of six installs
		// then asks for administrator rights once at the end instead of six times over.
		//
		// _files is filled either way - it is read out of the staging folder, which exists by then
		// whichever way the copy is going to be made.
		// A copy that was promised rather than made.
		struct PendingCopy
		{
			ElevatedTask::Job job;

			// The unpacked download. Named rather than worked out from the copy: job.sourceDir is this
			// folder for most builds and a folder inside it for the ones that carry a tree of their own,
			// so deleting "the parent of the source" would mean deleting the system temp folder.
			std::string stagingDir;
		};

		static bool install(const std::string& _zipPath, const std::string& _targetDir,
		                    const std::string& _pluginFileName, const std::string& _dataProduct, bool _withRights,
		                    std::vector<std::string>& _files, std::string& _error,
		                    PendingCopy* _pending = nullptr);

		// Copies what is in _sourceDir into _targetDir, replacing what is there under the same name
		// and leaving the rest alone. Also what the elevated half of an install runs.
		//
		// All of it or none of it: what is already there is checked first and then moved aside, so
		// that a copy which cannot finish leaves the folder as it found it. A bundle half replaced
		// is neither version and no host can load it.
		static bool copyInto(const std::string& _sourceDir, const std::string& _targetDir, std::string& _error);

		// Whether a build can be installed there at all. Checked before anything is downloaded, since
		// the answer does not depend on the download and finding out afterwards wastes it.
		static WriteAccess checkWriteAccess(const std::string& _targetDir, std::string& _error);

		static bool canWriteTo(const std::string& _targetDir, std::string& _error)
		{
			return checkWriteAccess(_targetDir, _error) == WriteAccess::Yes;
		}

	private:
		static juce::File pluginRoot(const juce::File& _unpacked, const std::string& _pluginFileName);
	};
}
