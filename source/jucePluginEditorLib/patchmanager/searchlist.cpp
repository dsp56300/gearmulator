#include "searchlist.h"

#include "list.h"

namespace jucePluginEditorLib::patchManager
{
	SearchList::SearchList(List& _list): m_list(_list)
	{
	}

	void SearchList::onTextChanged(const std::string& _text)
	{
		m_list.setFilter(_text);
	}
}
