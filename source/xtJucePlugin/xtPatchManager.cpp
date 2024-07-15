#include "xtPatchManager.h"

#include "xtController.h"
#include "xtEditor.h"

#include "jucePluginEditorLib/pluginProcessor.h"
#include "xtLib/xtMidiTypes.h"

namespace xtJucePlugin
{
	static constexpr std::initializer_list<jucePluginEditorLib::patchManager::GroupType> g_groupTypes =
	{
		jucePluginEditorLib::patchManager::GroupType::Favourites,
		jucePluginEditorLib::patchManager::GroupType::LocalStorage,
		jucePluginEditorLib::patchManager::GroupType::DataSources,
	};

	PatchManager::PatchManager(Editor& _editor, juce::Component* _root, const juce::File& _dir)
		: jucePluginEditorLib::patchManager::PatchManager(_editor, _root, _dir, g_groupTypes)
		, m_editor(_editor)
		, m_controller(_editor.getXtController())
	{
		setTagTypeName(pluginLib::patchDB::TagType::CustomA, "MW Model");
		startLoaderThread();
		addGroupTreeItemForTag(pluginLib::patchDB::TagType::CustomA);
	}

	PatchManager::~PatchManager()
	{
		stopLoaderThread();
	}

	bool PatchManager::requestPatchForPart(pluginLib::patchDB::Data& _data, const uint32_t _part)
	{
		_data = m_controller.createSingleDump(xt::LocationH::SingleBankA, 0, static_cast<uint8_t>(_part));
		return !_data.empty();
	}

	bool PatchManager::loadRomData(pluginLib::patchDB::DataList& _results, uint32_t _bank, uint32_t _program)
	{
		return false;
	}

	pluginLib::patchDB::PatchPtr PatchManager::initializePatch(pluginLib::patchDB::Data&& _sysex)
	{
		if(_sysex.size() == xt::Mw1::g_singleDumpLength)
		{
			if(_sysex[1] == wLib::IdWaldorf && _sysex[2] == xt::IdMw1)
			{
				// MW1 single dump
				auto p = std::make_shared<pluginLib::patchDB::Patch>();

				p->name.resize(xt::Mw1::g_singleNameLength, ' ');
				memcpy(p->name.data(), &_sysex[xt::Mw1::g_singleNamePosition], xt::Mw1::g_singleNameLength);
				while(p->name.back() == ' ')
					p->name.pop_back();

				p->sysex = std::move(_sysex);

				p->tags.add(pluginLib::patchDB::TagType::CustomA, "MW1");
				return p;
			}
		}

		pluginLib::MidiPacket::Data data;
		pluginLib::MidiPacket::AnyPartParamValues parameters;
		if(!m_controller.parseSingle(data, parameters, _sysex))
			return {};

		auto p = std::make_shared<pluginLib::patchDB::Patch>();

		p->sysex = std::move(_sysex);
		p->name = m_controller.getSingleName(parameters);

		p->tags.add(pluginLib::patchDB::TagType::CustomA, "MW2");

		return p;
	}

	pluginLib::patchDB::Data PatchManager::prepareSave(const pluginLib::patchDB::PatchPtr& _patch) const
	{
		pluginLib::MidiPacket::Data data;
		pluginLib::MidiPacket::AnyPartParamValues parameterValues;

		if (!m_controller.parseSingle(data, parameterValues, _patch->sysex))
			return _patch->sysex;

		// apply name
		if (!_patch->getName().empty())
			m_controller.setSingleName(parameterValues, _patch->getName());

		// apply program
		uint32_t program = 0;
		uint32_t bank = 0;
		if(_patch->program != pluginLib::patchDB::g_invalidProgram)
		{
			program = std::clamp(_patch->program, 0u, 299u);

			bank = program / 128;
			program -= bank * 128;
		}

		return m_controller.createSingleDump(static_cast<xt::LocationH>(static_cast<uint8_t>(xt::LocationH::SingleBankA) + bank), static_cast<uint8_t>(program), parameterValues);
	}

	bool PatchManager::equals(const pluginLib::patchDB::PatchPtr& _a, const pluginLib::patchDB::PatchPtr& _b) const
	{
		if(_a == _b)
			return true;

		if(_a->hash == _b->hash)
			return true;

		return false;
	}

	uint32_t PatchManager::getCurrentPart() const
	{
		return m_editor.getProcessor().getController().getCurrentPart();
	}

	bool PatchManager::activatePatch(const pluginLib::patchDB::PatchPtr& _patch)
	{
		m_controller.sendSingle(_patch->sysex);
		return true;
	}

	bool PatchManager::activatePatch(const pluginLib::patchDB::PatchPtr& _patch, const uint32_t _part)
	{
		if(!m_controller.sendSingle(_patch->sysex, static_cast<uint8_t>(_part)))
		{
			juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, 
				m_editor.getProcessor().getProperties().name + " - Unable to load patch",
				"MW1 patches can only be loaded to the first part.\n"
				"\n"
				"If you want to load a MW1 patch to another part, first convert it by loading it to part 1, then save the loaded patch to a user bank."
				, nullptr, juce::ModalCallbackFunction::create([](int){}));
		}
		return true;
	}
}
