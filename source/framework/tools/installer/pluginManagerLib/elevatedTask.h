#pragma once

#include <string>
#include <vector>

namespace pluginManager
{
	// Doing one step of an install with rights this program does not have, because the folder the
	// plugin goes into belongs to the system - C:\Program Files\Common Files\VST3 and
	// /Library/Audio/Plug-Ins are the usual ones.
	//
	// Only that step. The build is downloaded and unpacked as the user who started the program, and
	// what runs with more rights is a copy of files that are already on the disk. So nothing from the
	// network is ever fetched elevated, and the user is only asked for a folder that needs it.
	//
	// Windows asks by starting this same program again through UAC, with a command line that says
	// what to do; macOS asks the system to run a copy command after its own authorisation prompt.
	class ElevatedTask
	{
	public:
		// Whether the user can be asked here at all. On Linux there is no way of asking that works
		// whatever desktop is running, so there the answer stays "pick a location you own".
		static bool isSupported();

		// True when this process was started to do the work below, in which case it has done it and
		// the program must return _exitCode instead of starting up. _args are the command line
		// parameters, without the name of the program itself.
		static bool isWorkerRequest(const std::vector<std::string>& _args);
		static bool runAsWorker(const std::vector<std::string>& _args, int& _exitCode);

		// One copy that is waiting to be made. A whole install run collects these and hands them over
		// together, so that installing six plugins into a system folder asks once rather than six
		// times - which is what people actually complained about.
		struct Job
		{
			std::string sourceDir;		// a staging folder this user unpacked, never anything from the network
			std::string targetDir;
		};

		// Copies what is in _sourceDir into _targetDir, and deletes one file or folder. Both ask the
		// user for the rights first and block until that is answered and the work is through.
		static bool copyInto(const std::string& _sourceDir, const std::string& _targetDir, std::string& _error);
		static bool remove(const std::string& _path, std::string& _error);

		// The same copy, several times over, behind ONE prompt. _done says per job whether its files
		// arrived, so a run that was allowed but half failed can still record what did land.
		//
		// False when the prompt was declined or the elevated copy never ran; then nothing was done and
		// every entry of _done is false. True means it ran - read _done for what it managed.
		static bool copyIntoMany(const std::vector<Job>& _jobs, std::vector<bool>& _done, std::string& _error);

		// Several files or folders removed behind ONE prompt - the other half of copyIntoMany, for an
		// uninstall that takes a product out of a system folder in four formats, each with a changelog
		// beside it. _done says per path whether it is gone, asked of the file system afterwards: a
		// plugin a host has open is the usual one that is not, and that is worth saying per plugin.
		static bool removeMany(const std::vector<std::string>& _paths, std::vector<bool>& _done, std::string& _error);
	};
}
