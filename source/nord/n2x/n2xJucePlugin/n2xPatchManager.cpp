#include "n2xPatchManager.h"

#include "n2xController.h"
#include "n2xEditor.h"
#include "n2xLib/n2xmiditypes.h"

namespace n2xJucePlugin
{
	static constexpr std::initializer_list<jucePluginEditorLib::patchManager::GroupType> g_groupTypes =
	{
		jucePluginEditorLib::patchManager::GroupType::Favourites,
		jucePluginEditorLib::patchManager::GroupType::LocalStorage,
		jucePluginEditorLib::patchManager::GroupType::DataSources,
	};

	PatchManager::PatchManager(Editor& _editor, Component* _root, const juce::File& _dir)
	: jucePluginEditorLib::patchManager::PatchManager(_editor, _root, _dir, g_groupTypes)
	, m_editor(_editor)
	, m_controller(_editor.getN2xController())
	{
		startLoaderThread();
	}

	PatchManager::~PatchManager()
	{
		stopLoaderThread();
	}

	bool PatchManager::requestPatchForPart(pluginLib::patchDB::Data& _data, uint32_t _part)
	{
		_data = m_controller.createSingleDump(n2x::SysexByte::SingleDumpBankA, 0, static_cast<uint8_t>(_part));
		return !_data.empty();
	}

	bool PatchManager::loadRomData(pluginLib::patchDB::DataList& _results, uint32_t _bank, uint32_t _program)
	{
		return false;
	}

	pluginLib::patchDB::PatchPtr PatchManager::initializePatch(pluginLib::patchDB::Data&& _sysex)
	{
		const auto isSingle = _sysex.size() == n2x::g_singleDumpSize;
		const auto isMulti = _sysex.size() == n2x::g_multiDumpSize;

		if(!isSingle && !isMulti)
			return {};

		auto p = std::make_shared<pluginLib::patchDB::Patch>();

		const auto bank = _sysex[n2x::SysexIndex::IdxMsgType];
		const auto program = _sysex[n2x::SysexIndex::IdxMsgSpec];

		char name[128]{0};

		if(isSingle)
		{
			(void)snprintf(name, sizeof(name), "%c.%02d", bank == n2x::SingleDumpBankEditBuffer ? 'e' : ('1' + bank), program);
		}
		else
		{
			(void)snprintf(name, sizeof(name), "P%c.%02d", bank == n2x::MultiDumpBankEditBuffer ? 'e' : ('1' + bank), program);
		}

		p->name = name;
		p->sysex = std::move(_sysex);
		p->program = program;
		p->bank = bank;

		return p;
	}

	pluginLib::patchDB::Data PatchManager::prepareSave(const pluginLib::patchDB::PatchPtr& _patch) const
	{
		auto d = _patch->sysex;

		d[n2x::SysexIndex::IdxMsgType] = static_cast<uint8_t>(_patch->bank);
		d[n2x::SysexIndex::IdxMsgSpec] = static_cast<uint8_t>(_patch->program);

		return d;
	}

	uint32_t PatchManager::getCurrentPart() const
	{
		return m_controller.getCurrentPart();
	}

	bool PatchManager::activatePatch(const pluginLib::patchDB::PatchPtr& _patch)
	{
		return activatePatch(_patch, getCurrentPart());
	}

	bool PatchManager::activatePatch(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part)
	{
		return m_controller.activatePatch(_patch->sysex, _part);
	}

	bool PatchManager::parseFileData(pluginLib::patchDB::DataList& _results, const pluginLib::patchDB::Data& _data)
	{
		return jucePluginEditorLib::patchManager::PatchManager::parseFileData(_results, _data);
	}
}
