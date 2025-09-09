#include "jePatchManager.h"

#include "jeEditor.h"

#include "jeController.h"

namespace jeJucePlugin
{
	PatchManager::PatchManager(Editor& _editor, Rml::Element* _rootElement)
	: jucePluginEditorLib::patchManager::PatchManager(_editor, _rootElement, DefaultGroupTypes)
	, m_editor(_editor)
	, m_processor(_editor.getProcessor())
	, m_controller(_editor.geJeController())
	{
		// TODO: can we detect if its a keyboard or a rack preset?
//		setTagTypeName(pluginLib::patchDB::TagType::CustomA, "JE Model");

		jucePluginEditorLib::patchManager::PatchManager::startLoaderThread();
	}

	bool PatchManager::requestPatchForPart(pluginLib::patchDB::Data& _data, uint32_t _part, uint64_t _userData)
	{
		return false;
	}

	bool PatchManager::loadRomData(pluginLib::patchDB::DataList& _results, uint32_t _bank, uint32_t _program)
	{
		return false;
	}

	pluginLib::patchDB::PatchPtr PatchManager::initializePatch(pluginLib::patchDB::Data&& _sysex, const std::string& _defaultPatchName)
	{
		return {};
	}

	pluginLib::patchDB::Data PatchManager::applyModifications(const pluginLib::patchDB::PatchPtr& _patch, const pluginLib::FileType& _fileType, pluginLib::ExportType _exportType) const
	{
		return {};
	}

	uint32_t PatchManager::getCurrentPart() const
	{
		return m_controller.getCurrentPart();
	}

	bool PatchManager::activatePatch(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part)
	{
		return false;
	}
}
