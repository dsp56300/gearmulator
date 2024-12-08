#include "mqPatchManager.h"

#include "mqController.h"
#include "mqEditor.h"
#include "jucePluginEditorLib/pluginProcessor.h"
#include "mqLib/mqstate.h"

namespace mqJucePlugin
{
	static constexpr std::initializer_list<jucePluginEditorLib::patchManager::GroupType> g_groupTypes =
	{
		jucePluginEditorLib::patchManager::GroupType::Favourites,
		jucePluginEditorLib::patchManager::GroupType::LocalStorage,
		jucePluginEditorLib::patchManager::GroupType::DataSources,
	};

	PatchManager::PatchManager(Editor& _editor, juce::Component* _root)
		: jucePluginEditorLib::patchManager::PatchManager(_editor, _root, g_groupTypes)
		, m_editor(_editor)
		, m_controller(_editor.getMqController())
	{
		startLoaderThread();
	}

	PatchManager::~PatchManager()
	{
		stopLoaderThread();
	}

	bool PatchManager::requestPatchForPart(pluginLib::patchDB::Data& _data, uint32_t _part, uint64_t)
	{
		_data = m_controller.createSingleDump(mqLib::MidiBufferNum::SingleBankA, static_cast<mqLib::MidiSoundLocation>(0), _part, _part);
		return !_data.empty();
	}

	bool PatchManager::loadRomData(pluginLib::patchDB::DataList& _results, uint32_t _bank, uint32_t _program)
	{
		return false;
	}

	pluginLib::patchDB::PatchPtr PatchManager::initializePatch(pluginLib::patchDB::Data&& _sysex, const std::string& _defaultPatchName)
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

	pluginLib::patchDB::Data PatchManager::applyModifications(const pluginLib::patchDB::PatchPtr& _patch) const
	{
		auto result = _patch->sysex;

		if (_patch->sysex.size() != std::tuple_size_v<mqLib::State::Single> && 
			_patch->sysex.size() != std::tuple_size_v<mqLib::State::SingleQ>)
			return result;

		if (!_patch->getName().empty())
			mqLib::State::setSingleName(result, _patch->getName());

		// first set tag is category
		const auto& tags = _patch->getTags(pluginLib::patchDB::TagType::Category).getAdded();

		std::string category;

		if(!tags.empty())
			category = *tags.begin();

		if (!category.empty())
			mqLib::State::setCategory(result, category);

		// apply program
		uint32_t program = 0;
		uint32_t bank = 0;
		if(_patch->program != pluginLib::patchDB::g_invalidProgram)
		{
			program = std::clamp(_patch->program, 0u, 299u);

			bank = program / 100;
			program -= bank * 100;
		}

		result[mqLib::IdxSingleBank] = static_cast<uint8_t>(bank);
		result[mqLib::IdxSingleProgram] = static_cast<uint8_t>(program);

		mqLib::State::updateChecksum(result);

		return result;
	}

	uint32_t PatchManager::getCurrentPart() const
	{
		return m_editor.getProcessor().getController().getCurrentPart();
	}

	bool PatchManager::activatePatch(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part)
	{
		m_controller.sendSingle(applyModifications(_patch), static_cast<uint8_t>(_part));
		return true;
	}
}
