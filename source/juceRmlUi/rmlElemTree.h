#pragma once

#include "rmlElement.h"
#include "rmlTree.h"

namespace juceRmlUi
{
	class ElemTree : public Element
	{
	public:
		explicit ElemTree(const Rml::String& _tag);

	private:
		Tree m_tree;
	};
}
