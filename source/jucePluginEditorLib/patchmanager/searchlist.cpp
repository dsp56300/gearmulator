#include "searchlist.h"

#include "listmodel.h"

namespace jucePluginEditorLib::patchManager
{
	SearchList::SearchList(ListModel& _list) : m_list(&_list)
	{
	}

	void SearchList::onTextChanged(const std::string& _text)
	{
		m_list->setFilter(_text);
	}

	void SearchList::setListModel(ListModel* _list)
	{
		if(m_list == _list)
			return;

		m_list = _list;
		m_list->setFilter(getSearchText());
	}
}
