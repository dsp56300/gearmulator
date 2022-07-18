#pragma once

#include "../../juceUiLib/condition.h"

namespace genericVirusUI
{
	class VirusEditor;

	class FxPage
	{
	public:
		explicit FxPage(const VirusEditor& _editor);
		~FxPage();
	private:
		std::unique_ptr<genericUI::Condition> m_conditionReverb;
		std::unique_ptr<genericUI::Condition> m_conditionDelay;
	};
}
