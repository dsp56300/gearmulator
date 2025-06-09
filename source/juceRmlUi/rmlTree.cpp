#include "rmlTree.h"

namespace juceRmlUi
{
	Tree::Tree() : m_root(*this)
	{
	}

	Tree::~Tree()
	{
		m_root.clear();
	}
}
