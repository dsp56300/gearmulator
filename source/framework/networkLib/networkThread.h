#pragma once

#include <memory>
#include <thread>

namespace networkLib
{
	class NetworkThread
	{
	protected:
		NetworkThread() = default;
		~NetworkThread();

		virtual void threadLoopFunc() {}
		virtual void threadFunc();

		bool exit() const { return m_exit; }
		void exit(bool _exit);

		void start();
		void stop();

	private:
		std::unique_ptr<std::thread> m_thread;
		bool m_exit = false;
	};
}
