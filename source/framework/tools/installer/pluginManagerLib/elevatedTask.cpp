#include "elevatedTask.h"

#include "zipExtractor.h"

#include "juce_core/juce_core.h"

#if JUCE_WINDOWS
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#endif

namespace pluginManager
{
	namespace
	{
		// What a command line says to the copy of this program that UAC starts. Not a documented
		// option: it is this program talking to itself.
		//
		//   --elevated-task <report file> <verb> <the verb's own arguments>
		//
		// The report file is where the copy behind the prompt writes why it could not finish.
		// ShellExecuteEx is the only way to ask for the rights and it leaves no pipe behind, so
		// without somewhere to write it the exit code would be the whole of its answer.
		constexpr char g_switch[] = "--elevated-task";
		constexpr char g_copy[] = "copy";
		constexpr char g_copyMany[] = "copymany";
		constexpr char g_remove[] = "remove";
		constexpr char g_removeMany[] = "removemany";

		// The two answers that mean nothing ran with the rights at all.
		constexpr char g_declined[] = "Administrator rights are needed to write there, and the request was declined.";
		constexpr char g_cannotAsk[] = "Could not ask for administrator rights.";

		juce::File fileOf(const std::vector<std::string>& _args, const size_t _index)
		{
			return _index < _args.size() ? juce::File(juce::String(_args[_index])) : juce::File();
		}

		// The job list, one line per path, source and target alternating. A line, not JSON: these are
		// paths, and the one character a path cannot contain on either system is the newline.
		// The same content, not merely the same name: a reinstall or an upgrade finds every name of the
		// new build already there in the old one, so a copy that never happened would read as done and
		// be written down as the new version. Files the target has besides - a ROM put into a bundle -
		// are not the staging folder's business.
		bool sameContent(const juce::File& _staged, const juce::File& _placed)
		{
			if (!_staged.isDirectory())
				return _placed.existsAsFile() && _staged.hasIdenticalContentTo(_placed);

			for (const auto& child : _staged.findChildFiles(juce::File::findFilesAndDirectories, false))
			{
				if (!sameContent(child, _placed.getChildFile(child.getFileName())))
					return false;
			}

			return _placed.isDirectory();
		}

		// Everything the staging folder holds is in the target folder. The one answer both the single
		// copy and the batch are judged by, because a process behind a prompt reports back differently
		// on each system and the files do not.
		bool arrived(const std::string& _sourceDir, const std::string& _targetDir)
		{
			return sameContent(juce::File(juce::String(_sourceDir)), juce::File(juce::String(_targetDir)));
		}

		juce::String jobsToText(const std::vector<ElevatedTask::Job>& _jobs)
		{
			juce::String text;

			for (const auto& job : _jobs)
				text << juce::String(job.sourceDir) << "\n" << juce::String(job.targetDir) << "\n";

			return text;
		}

		std::vector<ElevatedTask::Job> jobsFromText(const juce::String& _text)
		{
			juce::StringArray lines;
			lines.addLines(_text);

			std::vector<ElevatedTask::Job> jobs;

			// Two at a time, and an odd one at the end is a truncated file rather than a job.
			for (int i = 0; i + 1 < lines.size(); i += 2)
			{
				if (lines[i].isNotEmpty() && lines[i + 1].isNotEmpty())
					jobs.push_back({lines[i].toStdString(), lines[i + 1].toStdString()});
			}

			return jobs;
		}

#if JUCE_WINDOWS
		// ShellExecuteEx wants the thread it is called on to have a COM apartment, and an install runs
		// on a worker thread that has none. Left alone if the thread already has one in another mode.
		struct ScopedCom
		{
			const bool mine = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
			~ScopedCom() { if (mine) CoUninitialize(); }
		};

		// Starts this program again with the rights, which is what "runas" asks UAC for. Blocks until
		// the copy of it is through, because what it reports is this install's answer.
		//
		// _arguments are the verb and its own arguments; the switch and the report file are this
		// function's business, so that the two ends of the protocol are written down in one place.
		bool runElevated(const juce::StringArray& _arguments, std::string& _error)
		{
			const ScopedCom com;

			const auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName();

			// Made here rather than by the worker, so the file belongs to this user and can still be
			// read and removed once an administrator has written to it.
			const auto report = juce::File::getSpecialLocation(juce::File::tempDirectory)
				.getChildFile("tus-elevated-" + juce::String(juce::Random::getSystemRandom().nextInt64()) + ".txt");

			report.replaceWithText({});

			struct Cleanup
			{
				juce::File file;
				~Cleanup() { file.deleteFile(); }
			} cleanup{report};

			// A command line, not a list: every path is quoted, since the ones this is for have
			// spaces in them more often than not.
			juce::String parameters;

			juce::StringArray all{g_switch, report.getFullPathName()};
			all.addArray(_arguments);

			for (const auto& parameter : all)
				parameters << (parameters.isEmpty() ? "" : " ") << parameter.quoted();

			SHELLEXECUTEINFOW info = {};

			info.cbSize = sizeof(info);
			info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
			info.lpVerb = L"runas";
			info.lpFile = exe.toWideCharPointer();
			info.lpParameters = parameters.toWideCharPointer();
			info.nShow = SW_HIDE;

			if (!ShellExecuteExW(&info) || !info.hProcess)
			{
				_error = GetLastError() == ERROR_CANCELLED
					? g_declined
					: g_cannotAsk;
				return false;
			}

			WaitForSingleObject(info.hProcess, INFINITE);

			DWORD exitCode = 1;
			GetExitCodeProcess(info.hProcess, &exitCode);
			CloseHandle(info.hProcess);

			if (exitCode == 0)
				return true;

			// What it wrote before it gave up. Empty when it never got that far, and then there is
			// nothing to say beyond the fact itself.
			_error = report.loadFileAsString().trim().toStdString();

			if (_error.empty())
				_error = "The install was allowed to run as administrator but did not finish.";

			return false;
		}
#elif JUCE_MAC
		// Single quotes, so that nothing in a path is read as a shell character.
		juce::String shellQuoted(const std::string& _path)
		{
			return "'" + juce::String(_path).replace("'", "'\\''") + "'";
		}

		// One job as a shell command: every plugin in the staging folder put in place of the one
		// already there.
		//
		// Not a plain ditto of the whole folder, although that is one line. ditto copies the contents
		// of a folder into another and leaves whatever else is in it, so for a bundle the files of the
		// version being replaced stay inside the new one. On macOS that is fatal rather than untidy: a
		// bundle is sealed, and a file in it that the signature does not cover makes the system refuse
		// to load the plugin at all.
		//
		// So each one is copied in beside the one it replaces and then swapped for it. The old copy is
		// only removed once the new one is there in full, which is the same promise the unprivileged
		// half keeps: an install that cannot finish must not have started.
		juce::String replaceEntries(const std::string& _sourceDir, const std::string& _targetDir)
		{
			const auto target = juce::File(juce::String(_targetDir));

			juce::String script = "/bin/mkdir -p " + shellQuoted(_targetDir);

			for (const auto& entry : juce::File(juce::String(_sourceDir))
				.findChildFiles(juce::File::findFilesAndDirectories, false))
			{
				const auto placed = target.getChildFile(entry.getFileName()).getFullPathName().toStdString();
				const auto staged = placed + ".tus-new";

				script << "; /bin/rm -rf " << shellQuoted(staged)
					<< " && /usr/bin/ditto " << shellQuoted(entry.getFullPathName().toStdString())
					<< " " << shellQuoted(staged)
					<< " && /bin/rm -rf " << shellQuoted(placed)
					<< " && /bin/mv " << shellQuoted(staged) << " " << shellQuoted(placed);
			}

			return script;
		}

		// macOS has no UAC. What it has is this: the system asks for a password and then runs the
		// command, which is the mechanism every installer that writes to /Library uses.
		bool runWithPrivileges(const juce::String& _shellCommand, std::string& _error)
		{
			const auto escaped = _shellCommand.replace("\\", "\\\\").replace("\"", "\\\"");

			juce::ChildProcess process;

			if (!process.start(juce::StringArray{"/usr/bin/osascript", "-e",
				"do shell script \"" + escaped + "\" with administrator privileges"}))
			{
				_error = g_cannotAsk;
				return false;
			}

			// What it printed, not what it returned. ChildProcess::getExitCode answers 0 when it
			// cannot tell - which it cannot once the output has been read, because reading it to the
			// end reaps the child - and a cancelled prompt then reads as a copy that worked.
			// osascript says nothing at all when the script did its work.
			const auto output = process.readAllProcessOutput().trim();

			if (output.isEmpty())
				return true;

			// What AppleScript says when the prompt is cancelled. Everything else is passed on as it
			// came, since it is the reason the copy itself failed.
			_error = output.contains("-128")
				? g_declined
				: "Could not write there as administrator.\n\n" + output.toStdString();

			return false;
		}
#endif
	}

	bool ElevatedTask::isSupported()
	{
#if JUCE_WINDOWS || JUCE_MAC
		return true;
#else
		return false;
#endif
	}

	bool ElevatedTask::isWorkerRequest(const std::vector<std::string>& _args)
	{
		return !_args.empty() && _args.front() == g_switch;
	}

	bool ElevatedTask::runAsWorker(const std::vector<std::string>& _args, int& _exitCode)
	{
		if (!isWorkerRequest(_args))
			return false;

		// See g_switch for the shape of this: the report file comes before the verb, so it sits
		// in the same place whatever the verb turns out to be.
		const auto report = fileOf(_args, 1);
		const auto verb = _args.size() > 2 ? _args[2] : std::string();

		std::string error;
		auto done = false;

		if (verb == g_copy && _args.size() > 4)
		{
			done = ZipExtractor::copyInto(_args[3], _args[4], error);
		}
		else if (verb == g_copyMany && _args.size() > 3)
		{
			const auto jobFile = fileOf(_args, 3);
			const auto jobs = jobsFromText(jobFile.loadFileAsString());

			if (jobs.empty())
			{
				error = "The plugin manager asked for an install with nothing in it.";
			}
			else
			{
				// Every one of them, whatever happened to the one before: a folder that is open in a
				// host must not stop the other five from being installed while the rights are here.
				juce::String results;
				done = true;

				for (const auto& job : jobs)
				{
					std::string one;
					const auto ok = ZipExtractor::copyInto(job.sourceDir, job.targetDir, one);

					results << (ok ? "1" : "0") << "\n";

					if (!ok && error.empty())
						error = one;
				}

				// Beside the job file, which is where the other end looks for it. Written before the
				// report, so a caller that sees a failure still has the per-job answer.
				jobFile.withFileExtension(".done").replaceWithText(results);
			}
		}
		else if (verb == g_removeMany && _args.size() > 3)
		{
			juce::StringArray paths;
			paths.addLines(fileOf(_args, 3).loadFileAsString());
			paths.removeEmptyStrings();

			done = !paths.isEmpty();

			if (!done)
				error = "The plugin manager asked for a removal with nothing in it.";

			// Every one of them, whatever happened to the one before: a plugin a host has open must not
			// keep the others in place while the rights are here. Which ones went is read from the disk
			// by the other end, so nothing per path has to come back.
			for (const auto& path : paths)
			{
				const auto file = juce::File(path);

				if (file.deleteRecursively() || !file.exists())
					continue;

				done = false;

				if (error.empty())
					error = "Could not remove " + path.toStdString() + ".\n\nIt may be open in a host.";
			}
		}
		else if (verb == g_remove && _args.size() > 3)
		{
			const auto file = fileOf(_args, 3);

			done = file.deleteRecursively();

			if (!done)
			{
				error = "Could not remove " + file.getFullPathName().toStdString() +
					".\n\nIt may be open in a host.";
			}
		}
		else
		{
			error = "The plugin manager asked itself for something it does not understand.";
		}

		// The only way back: the process that asked is waiting on the exit code and reads this
		// afterwards. Nothing is written when it worked - there is nothing to say.
		if (!done && report != juce::File())
			report.replaceWithText(juce::String(error));

		_exitCode = done ? 0 : 1;

		return true;
	}

	bool ElevatedTask::copyInto(const std::string& _sourceDir, const std::string& _targetDir, std::string& _error)
	{

#if JUCE_WINDOWS
		if (!runElevated({g_copy, juce::String(_sourceDir), juce::String(_targetDir)}, _error))
			return false;
#elif JUCE_MAC
		if (!runWithPrivileges(replaceEntries(_sourceDir, _targetDir), _error))
			return false;
#else
		_error = "This system has no way of asking for administrator rights.";
		return false;
#endif

		// Asked and answered is not the same as done. What ran with the rights was another process
		// behind a prompt, and neither system reports back the same way, so the files decide.
		if (arrived(_sourceDir, _targetDir))
			return true;

		_error = "Nothing arrived in " + _targetDir + ", although the rights for it were given.";

		return false;
	}

	bool ElevatedTask::copyIntoMany(const std::vector<Job>& _jobs, std::vector<bool>& _done, std::string& _error)
	{
		_done.assign(_jobs.size(), false);

		if (_jobs.empty())
			return true;

		// One of them is the case this is a generalisation of, and it already asks exactly once.
		if (_jobs.size() == 1)
		{
			_done[0] = copyInto(_jobs.front().sourceDir, _jobs.front().targetDir, _error);
			return _done[0];
		}

		// Written by this user, so it can still be read and removed after an administrator has
		// answered beside it - the same reason the report file is made out here.
		const auto jobFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("tus-elevated-jobs-" + juce::String(juce::Random::getSystemRandom().nextInt64()) + ".txt");

		const auto doneFile = jobFile.withFileExtension(".done");

		struct Cleanup
		{
			juce::File jobs, done;
			~Cleanup() { jobs.deleteFile(); done.deleteFile(); }
		} cleanup{jobFile, doneFile};

		if (!jobFile.replaceWithText(jobsToText(_jobs)))
		{
			_error = "Could not write the list of plugins to install.";
			return false;
		}

		std::string runError;

#if JUCE_WINDOWS
		const auto ran = runElevated({g_copyMany, jobFile.getFullPathName()}, runError);
#elif JUCE_MAC
		// One authorisation, one shell command: every job joined into it, so the prompt comes up once.
		juce::String script;

		for (const auto& job : _jobs)
			script << (script.isEmpty() ? "" : "; ") << replaceEntries(job.sourceDir, job.targetDir);

		const auto ran = runWithPrivileges(script, runError);
#else
		const auto ran = false;
		runError = "This system has no way of asking for administrator rights.";
#endif

		// Whatever it says, the files decide: this reads the same way for the one that ran as
		// administrator as it does for a copy made here.
		//
		// Unless nothing ran: a declined prompt copied nothing, whatever a same build already installed
		// there would make the files say.
		const auto nothingRan = runError == g_declined || runError == g_cannotAsk;

		auto all = true;

		for (size_t i = 0; i < _jobs.size(); ++i)
		{
			_done[i] = !nothingRan && arrived(_jobs[i].sourceDir, _jobs[i].targetDir);
			all = all && _done[i];
		}

		if (all)
			return true;

		_error = runError.empty()
			? "Some plugins did not arrive, although the rights for them were given."
			: runError;

		return false;
	}

	bool ElevatedTask::removeMany(const std::vector<std::string>& _paths, std::vector<bool>& _done, std::string& _error)
	{
		_done.assign(_paths.size(), false);

		if (_paths.empty())
			return true;

		std::string runError;

		// One of them is the case this generalises, and it already has a verb of its own.
		if (_paths.size() == 1)
		{
			remove(_paths.front(), runError);
		}
		else
		{
#if JUCE_WINDOWS
			// Written by this user, so it can be read and removed after an administrator has answered -
			// the same reason the job file of a copy is made out here. A path cannot hold a newline.
			const auto jobFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
				.getChildFile("tus-elevated-remove-" + juce::String(juce::Random::getSystemRandom().nextInt64()) + ".txt");

			struct Cleanup
			{
				juce::File file;
				~Cleanup() { file.deleteFile(); }
			} cleanup{jobFile};

			juce::StringArray lines;

			for (const auto& path : _paths)
				lines.add(juce::String(path));

			if (!jobFile.replaceWithText(lines.joinIntoString("\n")))
				runError = "Could not write the list of files to remove.";
			else
				runElevated({g_removeMany, jobFile.getFullPathName()}, runError);
#elif JUCE_MAC
			// One authorisation, one shell command, so the prompt comes up once.
			juce::String script;

			for (const auto& path : _paths)
				script << (script.isEmpty() ? "" : "; ") << "/bin/rm -rf " << shellQuoted(path);

			runWithPrivileges(script, runError);
#else
			runError = "This system has no way of asking for administrator rights.";
#endif
		}

		// Whatever came back, the disk decides: a prompt that was answered is not the same as files
		// that are gone, and one of them may be held open by a host.
		auto all = true;

		for (size_t i = 0; i < _paths.size(); ++i)
		{
			_done[i] = !juce::File(juce::String(_paths[i])).exists();
			all = all && _done[i];
		}

		if (!all)
			_error = runError.empty() ? "Some files could not be removed, although the rights for them were given." : runError;

		return all;
	}

	bool ElevatedTask::remove(const std::string& _path, std::string& _error)
	{
#if JUCE_WINDOWS
		if (!runElevated({g_remove, juce::String(_path)}, _error))
			return false;
#elif JUCE_MAC
		if (!runWithPrivileges("/bin/rm -rf " + shellQuoted(_path), _error))
			return false;
#else
		_error = "This system has no way of asking for administrator rights.";
		return false;
#endif

		if (!juce::File(juce::String(_path)).exists())
			return true;

		_error = _path + " is still there, although the rights to remove it were given.";

		return false;
	}
}
