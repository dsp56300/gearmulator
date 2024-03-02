#pragma once

#include "search.h"

namespace jucePluginEditorLib::patchManager
{
	class List;

	class SearchList : public Search
	{
	public:
		explicit SearchList(List& _list);

		void onTextChanged(const std::string& _text) override;

	private:
		List& m_list;
	};
}
