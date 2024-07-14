#pragma once

#include "search.h"

namespace jucePluginEditorLib::patchManager
{
	class ListModel;

	class SearchList : public Search
	{
	public:
		explicit SearchList(ListModel& _list);

		void onTextChanged(const std::string& _text) override;


		void setListModel(ListModel* _list);

	private:
		ListModel* m_list;
	};
}
