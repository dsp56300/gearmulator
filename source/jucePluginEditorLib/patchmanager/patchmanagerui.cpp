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

	void PatchManagerUi::sortPatches(std::vector<pluginLib::patchDB::PatchPtr>& _patches, pluginLib::patchDB::SourceType _sourceType)
	{
		std::sort(_patches.begin(), _patches.end(), [_sourceType](const pluginLib::patchDB::PatchPtr& _a, const pluginLib::patchDB::PatchPtr& _b)
		{
			const auto sourceType = _sourceType;

			if(sourceType == pluginLib::patchDB::SourceType::Folder)
			{
				const auto aSource = _a->source.lock();
				const auto bSource = _b->source.lock();
				if (*aSource != *bSource)
					return *aSource < *bSource;
			}
			else if (sourceType == pluginLib::patchDB::SourceType::File || sourceType == pluginLib::patchDB::SourceType::Rom || sourceType == pluginLib::patchDB::SourceType::LocalStorage)
			{
				if (_a->program != _b->program)
					return _a->program < _b->program;
			}

			return _a->getName().compare(_b->getName()) < 0;
		});
	}

}
