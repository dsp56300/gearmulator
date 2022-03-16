#pragma once

#include "VirusPatchBrowser.h"

namespace genericVirusUI
{
	class VirusEditor;

	class PatchBrowser
	{
	public:
		explicit PatchBrowser(const VirusEditor& _editor);
	private:
		::PatchBrowser m_patchBrowser;
	};
}
