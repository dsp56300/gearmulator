#include "searchlist.h"

#include "listmodel.h"

namespace jucePluginEditorLib::patchManager
{
	SearchList::SearchList(ListModel& _list): m_list(_list)
	{
	}

	void SearchList::onTextChanged(const std::string& _text)
	{
		m_list.setFilter(_text);
	}
}
