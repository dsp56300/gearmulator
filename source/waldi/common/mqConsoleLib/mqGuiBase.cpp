#include "mqGuiBase.h"

#include <iostream>

#include <cpp-terminal/base.hpp>

namespace mqConsoleLib
{
	bool GuiBase::handleTerminalSize()
	{
		int w = -1, h = -1;
		Term::get_term_size(w, h);

		if(w == m_termSizeX && h == m_termSizeY)
			return false;

		std::cout << Term::clear_screen();

		m_termSizeX = w;
		m_termSizeY = h;

		return true;
	}
}