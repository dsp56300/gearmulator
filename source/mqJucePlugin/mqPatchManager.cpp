#include "mqPatchManager.h"

#include "mqController.h"
#include "mqEditor.h"
#include "jucePluginEditorLib/pluginProcessor.h"

namespace mqJucePlugin
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
		, m_controller(_editor.getMqController())
	{
		startLoaderThread();
	}

	PatchManager::~PatchManager()
	{
		stopLoaderThread();
	}

	bool PatchManager::requestPatchForPart(pluginLib::patchDB::Data& _data, uint32_t _part)
	{
		_data = m_controller.createSingleDump(mqLib::MidiBufferNum::SingleBankA, static_cast<mqLib::MidiSoundLocation>(0), _part, _part);
		return !_data.empty();
	}

	bool PatchManager::loadRomData(pluginLib::patchDB::DataList& _results, uint32_t _bank, uint32_t _program)
	{
		return false;
	}

	pluginLib::patchDB::PatchPtr PatchManager::initializePatch(pluginLib::patchDB::Data&& _sysex)
	{
		pluginLib::MidiPacket::Data data;
		pluginLib::MidiPacket::AnyPartParamValues parameters;
		if(!m_controller.parseSingle(data, parameters, _sysex))
			return {};

		auto p = std::make_shared<pluginLib::patchDB::Patch>();

		p->sysex = std::move(_sysex);
		p->name = m_controller.getSingleName(parameters);

		auto category = m_controller.getCategory(parameters);

		while(!category.empty() && isspace(category.back()))
			category.pop_back();
		while(!category.empty() && isspace(category.front()))
			category.erase(0);

		if(!category.empty())
			p->tags.add(pluginLib::patchDB::TagType::Category, category);

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

			bank = program / 100;
			program -= bank * 100;
		}

		// apply category
		const auto& tags = _patch->getTags(pluginLib::patchDB::TagType::Category).getAdded();

		if(!tags.empty())
			m_controller.setCategory(parameterValues, *tags.begin());

		return m_controller.createSingleDump(
			static_cast<mqLib::MidiBufferNum>(static_cast<uint8_t>(mqLib::MidiBufferNum::DeprecatedSingleBankA) + bank), 
			static_cast<mqLib::MidiSoundLocation>(static_cast<uint8_t>(mqLib::MidiSoundLocation::AllSinglesBankA) + bank),
			static_cast<uint8_t>(program), parameterValues);
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

	bool PatchManager::activatePatch(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part)
	{
		m_controller.sendSingle(_patch->sysex, static_cast<uint8_t>(_part));
		return true;
	}
}
