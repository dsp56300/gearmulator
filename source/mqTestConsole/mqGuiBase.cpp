#include "mqGuiBase.h"

#include <cpp-terminal/base.hpp>

bool GuiBase::handleTerminalSize()
{
	int w = -1, h = -1;
	Term::get_term_size(w, h);

	if(w == m_termSizeX && h == m_termSizeY)
		return false;

	Term::clear_screen();

	m_termSizeX = w;
	m_termSizeY = h;

	return true;
}
