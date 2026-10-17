#include "discoveryFile.h"

#include "mcpServer.h"

#include "networkLib/logging.h"

#include "juce_core/juce_core.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#include <cstdlib>

namespace mcpServer
{
	std::mutex DiscoveryFile::s_mutex;

	namespace
	{
		// Locks a file next to the discovery file. Its folder exists, so there is nothing two processes could
		// race to create first. juce::InterProcessLock has such a race on macOS: its lock files live in
		// ~/Library/Caches/com.juce.locks, and a process that loses the race to create that folder silently
		// locks a file in /tmp instead, which excludes nobody.
		// The OS releases the lock if the process dies while holding it.
		class FileLock
		{
		public:
			FileLock(const juce::File& _file, const uint32_t _timeoutMs)
			{
				const auto start = juce::Time::getMillisecondCounter();
#ifdef _WIN32
				m_handle = CreateFileW(_file.getFullPathName().toWideCharPointer(), GENERIC_READ | GENERIC_WRITE,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

				if (m_handle == INVALID_HANDLE_VALUE)
					return;

				for (;;)
				{
					OVERLAPPED overlapped{};
					if (LockFileEx(m_handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &overlapped))
					{
						m_locked = true;
						return;
					}
					if (GetLastError() != ERROR_LOCK_VIOLATION || juce::Time::getMillisecondCounter() - start >= _timeoutMs)
						return;
					juce::Thread::sleep(1);
				}
#else
				// O_CLOEXEC: a child process must not inherit the lock and keep it held after we let go
				m_fd = open(_file.getFullPathName().toRawUTF8(), O_RDWR | O_CREAT | O_CLOEXEC, 0666);

				if (m_fd < 0)
					return;

				for (;;)
				{
					if (flock(m_fd, LOCK_EX | LOCK_NB) == 0)
					{
						m_locked = true;
						return;
					}
					if ((errno != EWOULDBLOCK && errno != EINTR) || juce::Time::getMillisecondCounter() - start >= _timeoutMs)
						return;
					juce::Thread::sleep(1);
				}
#endif
			}

			~FileLock()
			{
#ifdef _WIN32
				if (m_handle == INVALID_HANDLE_VALUE)
					return;
				if (m_locked)
				{
					OVERLAPPED overlapped{};
					UnlockFileEx(m_handle, 0, 1, 0, &overlapped);
				}
				CloseHandle(m_handle);
#else
				if (m_fd >= 0)
					close(m_fd);
#endif
			}

			FileLock(const FileLock&) = delete;
			FileLock& operator=(const FileLock&) = delete;

			bool isLocked() const { return m_locked; }

		private:
#ifdef _WIN32
			HANDLE m_handle = INVALID_HANDLE_VALUE;
#else
			int m_fd = -1;
#endif
			bool m_locked = false;
		};

		void writeInstances(const std::vector<DiscoveryEntry>& _instances)
		{
			auto arr = JsonValue::array();

			for (const auto& inst : _instances)
			{
				auto obj = JsonValue::object();
				obj.set("pluginName", JsonValue::fromString(inst.pluginName));
				obj.set("plugin4CC", JsonValue::fromString(inst.plugin4CC));
				obj.set("port", JsonValue::fromInt(inst.port));
				obj.set("pid", JsonValue::fromInt(inst.pid));
				obj.set("sessionId", JsonValue::fromString(inst.sessionId));
				arr.append(obj);
			}

			juce::File(DiscoveryFile::getDiscoveryFilePath()).replaceWithText(juce::JSON::toString(arr.getVar(), true));
		}
	}

	std::string DiscoveryFile::getDiscoveryFilePath()
	{
		// lets a test work on a file of its own instead of the one every running instance shares
		const auto path = juce::SystemStats::getEnvironmentVariable("GEARMULATOR_MCP_DISCOVERY_FILE", {});
		if (path.isNotEmpty())
			return path.toStdString();

		const auto homeDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
		return homeDir.getChildFile(".gearmulator_mcp.json").getFullPathName().toStdString();
	}

	bool DiscoveryFile::registerInstance(const DiscoveryEntry& _entry)
	{
		return modifyInstances([&](std::vector<DiscoveryEntry>& _instances)
		{
			// Remove any stale entry on the same port. Only the port a server actually bound is ever
			// registered, so nobody else holds it: an entry naming it is left over from a process that
			// died without unregistering.
			_instances.erase(std::remove_if(_instances.begin(), _instances.end(),
				[&](const DiscoveryEntry& e) { return e.port == _entry.port; }), _instances.end());

			_instances.push_back(_entry);
		});
	}

	void DiscoveryFile::registerThisProcess(const std::string& _name, const std::string& _fourCC, const McpServer& _server)
	{
		DiscoveryEntry entry;

		entry.pluginName = _name;
		entry.plugin4CC = _fourCC;
		entry.port = _server.getPort();
		entry.pid = static_cast<int>(
#ifdef _WIN32
			GetCurrentProcessId()
#else
			getpid()
#endif
		);

		// Set by the client for every process it spawns, so the host of whatever we run in inherits
		// it. Lets a client pick its own instance out of the discovery file instead of guessing a
		// port. See scripts/mcpBridge.py
		if (const auto* sessionId = std::getenv("CLAUDE_CODE_SESSION_ID"))
			entry.sessionId = sessionId;

		registerInstance(entry);
	}

	void DiscoveryFile::unregisterInstance(const int _port)
	{
		modifyInstances([&](std::vector<DiscoveryEntry>& _instances)
		{
			_instances.erase(std::remove_if(_instances.begin(), _instances.end(),
				[&](const DiscoveryEntry& e) { return e.port == _port; }), _instances.end());
		});
	}

	bool DiscoveryFile::modifyInstances(const std::function<void(std::vector<DiscoveryEntry>&)>& _modify)
	{
		// s_mutex orders the threads of this process, the file lock the processes: every plugin host and the
		// Plugin Manager rewrite the same file, and two of them reading it at once would each write back a
		// list without the other's entry.
		std::lock_guard lock(s_mutex);

		// A holder that hangs (stopped in a debugger) must not hang a plugin constructor: after the timeout
		// the file is written unlocked, as it was before the lock existed.
		const auto path = getDiscoveryFilePath();
		const FileLock fileLock(juce::File(path + ".lock"), 5000);

		if (!fileLock.isLocked())
			LOGNET(networkLib::LogLevel::Warning, "Failed to lock " << path << ".lock, writing it unlocked");

		auto instances = readInstances();
		_modify(instances);
		writeInstances(instances);

		return fileLock.isLocked();
	}

	std::vector<DiscoveryEntry> DiscoveryFile::readInstances()
	{
		std::vector<DiscoveryEntry> result;

		const auto path = getDiscoveryFilePath();
		const juce::File file(path);

		if (!file.existsAsFile())
			return result;

		const auto text = file.loadFileAsString();
		const auto parsed = juce::JSON::parse(text);

		if (!parsed.isArray())
			return result;

		const auto* arr = parsed.getArray();
		for (const auto& item : *arr)
		{
			if (const auto* obj = item.getDynamicObject())
			{
				DiscoveryEntry entry;
				entry.pluginName = obj->getProperty("pluginName").toString().toStdString();
				entry.plugin4CC = obj->getProperty("plugin4CC").toString().toStdString();
				entry.port = static_cast<int>(obj->getProperty("port"));
				entry.pid = static_cast<int>(obj->getProperty("pid"));
				entry.sessionId = obj->getProperty("sessionId").toString().toStdString();
				result.push_back(entry);
			}
		}

		return result;
	}
}
