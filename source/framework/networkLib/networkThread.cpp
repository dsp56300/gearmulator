#include "networkThread.h"

namespace networkLib
{
	NetworkThread::~NetworkThread()
	{
		stop();
	}

	void NetworkThread::threadFunc()
	{
		while (!m_exit)
			threadLoopFunc();
	}

	void NetworkThread::exit(bool _exit)
	{
		m_exit = _exit;
	}

	void NetworkThread::start()
	{
		if(m_thread)
			return;
		m_exit = false;
		m_thread.reset(new std::thread([this]()
		{
			threadFunc();
		}));
	}

	void NetworkThread::stop()
	{
		if(!m_thread)
			return;
		m_exit = true;
		m_thread->join();
		m_thread.reset();
	}
}
