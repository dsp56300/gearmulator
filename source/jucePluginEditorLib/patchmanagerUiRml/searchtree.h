#pragma once

#include "search.h"

namespace jucePluginEditorLib::patchManagerRml
{
	class Tree;

	class SearchTree : public Search
	{
	public:
		explicit SearchTree(Rml::ElementFormControlInput* _input, Tree& _tree);

		void onTextChanged(const std::string& _text) override;

	private:
		Tree& m_tree;
	};
}
