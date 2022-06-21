#pragma once

class GuiBase
{
protected:
	bool handleTerminalSize();
private:
	int m_termSizeX = -1;
	int m_termSizeY = -1;
};
