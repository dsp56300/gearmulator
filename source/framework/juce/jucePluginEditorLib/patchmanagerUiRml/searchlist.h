#pragma once

#include "search.h"

namespace jucePluginEditorLib::patchManagerRml
{
	class ListModel;

	class SearchList : public Search
	{
	public:
		explicit SearchList(Rml::ElementFormControlInput* _input, ListModel& _list);

		void onTextChanged(const std::string& _text) override;

		void setListModel(ListModel* _list);

	private:
		ListModel* m_list;
	};
}
