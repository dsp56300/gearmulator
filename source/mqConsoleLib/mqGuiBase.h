#pragma once

namespace mqConsoleLib
{
	class GuiBase
	{
	public:
		virtual ~GuiBase() = default;
		virtual void onOpen() {}
	protected:
		bool handleTerminalSize();
	private:
		int m_termSizeX = -1;
		int m_termSizeY = -1;
	};
}
