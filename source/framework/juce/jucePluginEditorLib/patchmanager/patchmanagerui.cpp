#include "patchmanagerui.h"

#include "patchmanager.h"

namespace jucePluginEditorLib::patchManager
{
	PatchManagerUi::PatchManagerUi(Editor& _editor, PatchManager& _db): m_editor(_editor), m_db(_db)
	{
	}

	bool PatchManagerUi::isScanning() const
	{
		return m_db.isScanning();
	}
}
