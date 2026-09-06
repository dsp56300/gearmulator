#include "searchlist.h"

#include "listmodel.h"

namespace jucePluginEditorLib::patchManagerRml
{
	SearchList::SearchList(Rml::ElementFormControlInput* _input, ListModel& _list) : Search(_input), m_list(&_list)
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
