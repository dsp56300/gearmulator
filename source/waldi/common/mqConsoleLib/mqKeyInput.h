#pragma once

namespace mqLib
{
	class MicroQ;
}

namespace mqConsoleLib
{
	class KeyInput
	{
	public:
		KeyInput(mqLib::MicroQ& _mQ) : m_mq(_mQ)
		{
		}

		void processKey(int ch);

	private:
		mqLib::MicroQ& m_mq;
	};
}
