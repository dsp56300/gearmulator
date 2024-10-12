#include "xtPatchManager.h"

#include "xtController.h"
#include "xtEditor.h"
#include "xtWaveEditor.h"

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

	bool PatchManager::requestPatchForPart(pluginLib::patchDB::Data& _data, const uint32_t _part, uint64_t)
	{
		_data = m_controller.createSingleDump(xt::LocationH::SingleBankA, 0, static_cast<uint8_t>(_part));
		_data = createCombinedDump(_data);
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

		bool hasUserTable = false;
		bool hasUserWaves = false;

		if(_sysex.size() > std::tuple_size_v<xt::State::Single>)
		{
			std::vector<xt::SysEx> dumps;
			xt::State::splitCombinedPatch(dumps, _sysex);

			if(dumps.empty())
				return {};

			if(!m_controller.parseSingle(data, parameters, dumps.front()))
				return {};

			if(dumps.size() > 2)
				hasUserWaves = true;
			hasUserTable = true;
		}
		else if(!m_controller.parseSingle(data, parameters, _sysex))
		{
			return {};
		}

		auto p = std::make_shared<pluginLib::patchDB::Patch>();

		p->sysex = std::move(_sysex);
		p->name = m_controller.getSingleName(parameters);

		p->tags.add(pluginLib::patchDB::TagType::CustomA, "MW2");

		if(hasUserTable)
			p->tags.add(pluginLib::patchDB::TagType::Tag, "UserTable");

		if(hasUserWaves)
			p->tags.add(pluginLib::patchDB::TagType::Tag, "UserWave");

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

		const auto sysex = m_controller.createSingleDump(static_cast<xt::LocationH>(static_cast<uint8_t>(xt::LocationH::SingleBankA) + bank), static_cast<uint8_t>(program), parameterValues);
		return createCombinedDump(sysex);
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

	bool PatchManager::parseFileData(pluginLib::patchDB::DataList& _results, const pluginLib::patchDB::Data& _data)
	{
		if(!jucePluginEditorLib::patchManager::PatchManager::parseFileData(_results, _data))
			return false;

		// check if there are MW1 bank dumps. A bank dump is one sysex with multiple patches. Split them into individual preset dumps
		const int resultCount = static_cast<int>(_results.size());

		for(int r=0; r<resultCount; ++r)
		{
			auto& res = _results[r];

			if(res.size() < xt::Mw1::g_singleDumpLength)
				continue;

			if(res[0] != 0xf0 || res[1] != wLib::IdWaldorf || res[2] != xt::IdMw1)
				continue;

			auto createPreset = [](pluginLib::patchDB::DataList& _res, const std::vector<uint8_t>& _source, size_t _readPos)
			{
				pluginLib::patchDB::Data data;

				constexpr uint8_t deviceNum = 0;

				// create single dump preset header
				data.reserve(xt::Mw1::g_singleDumpLength);
				data.assign({0xf0, wLib::IdWaldorf, xt::IdMw1, deviceNum, xt::Mw1::g_idmPreset});

				// add data
				uint8_t checksum = 0;

				for(size_t j=0; j<xt::Mw1::g_singleLength; ++j, ++_readPos)
				{
					const auto d = _source[_readPos];
					checksum += d;
					data.push_back(d);
				}

				// add checksum and EOX. FWIW the XT ignores the checksum anyway
				data.push_back(checksum & 0x7f);
				data.push_back(0xf7);

				_res.push_back(std::move(data));
				return _readPos;
			};

			if(res[4] == xt::Mw1::g_idmPresetBank)
			{
				// remove bank from results
				const auto source = std::move(res);
				_results.erase(_results.begin() + r);
				--r;

				const auto rawDataSize = source.size() - xt::Mw1::g_sysexHeaderSize - xt::Mw1::g_sysexFooterSize;
				const auto presetCount = rawDataSize / xt::Mw1::g_singleLength;

				size_t readPos = xt::Mw1::g_sysexHeaderSize;

				_results.reserve(presetCount);

				for(size_t i=0; i<presetCount; ++i)
					readPos = createPreset(_results, source, readPos);
			}
			else if(res[4] == xt::Mw1::g_idmCartridgeBank)
			{
				// remove bank from results
				const auto source = std::move(res);
				_results.erase(_results.begin() + r);
				--r;

				size_t readPos = 5;
				_results.reserve(64);
				for(size_t p=0; p<64; ++p)
					readPos = createPreset(_results, source, readPos);
			}
		}

		return true;
	}

	pluginLib::patchDB::Data PatchManager::createCombinedDump(const pluginLib::patchDB::Data& _data) const
	{
		// combine single dump with user wave table and user waves if applicable
		if(auto* waveEditor = m_editor.getWaveEditor())
		{
			std::vector<xt::SysEx> results;
			waveEditor->getData().getWaveDataForSingle(results, _data);
			if(!results.empty())
			{
				results.insert(results.begin(), _data);
				auto result = xt::State::createCombinedPatch(results);
				if(!result.empty())
					return result;
			}
		}
		return _data;
	}
}
