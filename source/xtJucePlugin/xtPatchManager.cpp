#include "xtPatchManager.h"

#include "xtController.h"
#include "xtEditor.h"
#include "xtWaveEditor.h"

#include "jucePluginEditorLib/pluginProcessor.h"

#include "jucePluginLib/filetype.h"

#include "juceUiLib/messageBox.h"

#include "xtLib/xtMidiTypes.h"

#include "synthLib/midiToSysex.h"

namespace xtJucePlugin
{
	static constexpr std::initializer_list<jucePluginEditorLib::patchManager::GroupType> g_groupTypes =
	{
		jucePluginEditorLib::patchManager::GroupType::Favourites,
		jucePluginEditorLib::patchManager::GroupType::MidiBanks,
		jucePluginEditorLib::patchManager::GroupType::LocalStorage,
		jucePluginEditorLib::patchManager::GroupType::DataSources,
	};

	PatchManager::PatchManager(Editor& _editor, Rml::Element* _root)
		: jucePluginEditorLib::patchManager::PatchManager(_editor, _root, g_groupTypes)
		, m_editor(_editor)
		, m_controller(_editor.getXtController())
	{
		setTagTypeName(pluginLib::patchDB::TagType::CustomA, "MW Model");
		jucePluginEditorLib::patchManager::PatchManager::startLoaderThread();
		addGroupTreeItemForTag(pluginLib::patchDB::TagType::CustomA);
		addGroupTreeItemForTag(pluginLib::patchDB::TagType::CustomC);
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

	pluginLib::patchDB::PatchPtr PatchManager::initializePatch(pluginLib::patchDB::Data&& _sysex, const std::string& _defaultPatchName)
	{
		const auto patchType = detectPatchType(_sysex);

		if (patchType == PatchType::Multi || patchType == PatchType::Arrangement)
		{
			auto patch = std::make_shared<pluginLib::patchDB::Patch>();
			patch->sysex = std::move(_sysex);
			patch->name = extractMultiName(patch->sysex);
			if (patch->name.empty())
				patch->name = _defaultPatchName.empty() ? "Multi" : _defaultPatchName;
			patch->tags.add(pluginLib::patchDB::TagType::CustomC,
				patchType == PatchType::Multi ? "Multi" : "Arrangement");
			return patch;
		}

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
				p->tags.add(pluginLib::patchDB::TagType::CustomC, "Single");
				return p;
			}
		}

		pluginLib::MidiPacket::Data data;
		pluginLib::MidiPacket::AnyPartParamValues parameters;

		bool hasUserTable = false;
		bool hasUserWaves = false;

		if(_sysex.size() > std::tuple_size_v<xt::State::Single>)
		{
			synthLib::SysexBufferList dumps;
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
		p->tags.add(pluginLib::patchDB::TagType::CustomC, "Single");

		if(hasUserTable)
			p->tags.add(pluginLib::patchDB::TagType::Tag, "UserTable");

		if(hasUserWaves)
			p->tags.add(pluginLib::patchDB::TagType::Tag, "UserWave");

		return p;
	}

	pluginLib::patchDB::Data PatchManager::applyModifications(const pluginLib::patchDB::PatchPtr& _patch, const pluginLib::FileType& _fileType, pluginLib::ExportType _exportType) const
	{
		auto applyModifications = [&_patch](pluginLib::patchDB::Data& _result) -> bool
		{
			if (xt::State::getCommand(_result) != xt::SysexCommand::SingleDump)
				return false;

			const auto dumpSize = _result.size();

			if (dumpSize != std::tuple_size_v<xt::State::Single>)
				return false;

			// apply name
			if (!_patch->getName().empty())
				xt::State::setSingleName(_result, _patch->getName());

			// apply program
			uint32_t program = 0;
			uint32_t bank = 0;
			if(_patch->program != pluginLib::patchDB::g_invalidProgram)
			{
				program = std::clamp(_patch->program, 0u, 299u);

				bank = program / 128;
				program -= bank * 128;
			}

			_result[xt::SysexIndex::IdxSingleBank   ] = static_cast<uint8_t>(bank);
			_result[xt::SysexIndex::IdxSingleProgram] = static_cast<uint8_t>(program);

			xt::State::updateChecksum(_result, xt::SysexIndex::IdxSingleChecksumStart);

			return true;
		};

		if (xt::State::getCommand(_patch->sysex) == xt::SysexCommand::SingleDump)
		{
			auto result = _patch->sysex;

			if (applyModifications(result))
				return result;

			std::vector<xt::SysEx> dumps;

			if (xt::State::splitCombinedPatch(dumps, _patch->sysex))
			{
				if (applyModifications(dumps[0]))
				{
					if (_exportType == pluginLib::ExportType::File)
					{
						// hardware compatibility: multiple sysex
						xt::SysEx r;

						for (auto& dump : dumps)
							r.insert(r.end(), dump.begin(), dump.end());

						return r;
					}

					// emu compatibility: custom patch format, one sysex that includes everything
					return xt::State::createCombinedPatch(dumps);
				}
			}
		}

		return _patch->sysex;
	}

	uint32_t PatchManager::getCurrentPart() const
	{
		return m_editor.getProcessor().getController().getCurrentPart();
	}

	bool PatchManager::activatePatch(const pluginLib::patchDB::PatchPtr& _patch, const uint32_t _part)
	{
		const auto sysex = applyModifications(_patch, pluginLib::FileType::Empty, pluginLib::ExportType::EmuHardware);
		const auto type = detectPatchType(sysex);

		switch (type)
		{
		case PatchType::Multi:
			return activateMulti(sysex);
		case PatchType::Arrangement:
			return activateArrangement(sysex);
		case PatchType::Single:
		default:
			return activateSingle(_patch, _part);
		}
	}

	bool PatchManager::parseFileData(pluginLib::patchDB::DataList& _results, const pluginLib::patchDB::Data& _data, const std::string& _filename)
	{
		if(!jucePluginEditorLib::patchManager::PatchManager::parseFileData(_results, _data, _filename))
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

			auto createPreset = [](pluginLib::patchDB::DataList& _res, const synthLib::SysexBuffer& _source, size_t _readPos)
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

		// Detect multi+8singles arrangement pattern and merge into compounds
		{
			pluginLib::patchDB::DataList merged;
			auto isSingle = [](const pluginLib::patchDB::Data& d)
			{
				return d.size() >= 8 && xt::State::getCommand(d) == xt::SysexCommand::SingleDump;
			};
			auto isMulti = [](const pluginLib::patchDB::Data& d)
			{
				return d.size() >= 8 && xt::State::getCommand(d) == xt::SysexCommand::MultiDump;
			};

			for (size_t i = 0; i < _results.size();)
			{
				if (isMulti(_results[i]) && i + m_controller.getPartCount() < _results.size())
				{
					bool allSingles = true;
					for (size_t j = 1; j <= m_controller.getPartCount(); ++j)
					{
						if (!isSingle(_results[i + j]))
						{
							allSingles = false;
							break;
						}
					}
					if (allSingles)
					{
						pluginLib::patchDB::Data compound = _results[i];
						for (size_t j = 1; j <= m_controller.getPartCount(); ++j)
							compound.insert(compound.end(), _results[i + j].begin(), _results[i + j].end());
						merged.emplace_back(std::move(compound));
						i += 1 + m_controller.getPartCount();
						continue;
					}
				}
				merged.emplace_back(std::move(_results[i]));
				++i;
			}
			_results = std::move(merged);
		}

		createCombinedDumps(_results);

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

	void PatchManager::createCombinedDumps(std::vector<pluginLib::patchDB::Data>& _messages)
	{
		// grab single dumps, waves and control tables and combine them into our custom single format that includes waves & tables if applicable

		m_waves.clear();
		m_tables.clear();
		m_singles.clear();

		// grab waves & tables first, if there are none we can skip everything
		for (auto& msg : _messages)
		{
			auto cmd = xt::State::getCommand(msg);
			switch (cmd)
			{
			case xt::SysexCommand::WaveDump:
				{
					const auto id = xt::State::getWaveId(msg);
					if (m_waves.find(id) == m_waves.end())
					{
						m_waves.emplace(id, std::move(msg));
						msg.clear();
					}
				}
				break;
			case xt::SysexCommand::WaveCtlDump:
				{
					const auto id = xt::State::getTableId(msg);
					if (m_tables.find(id) == m_tables.end())
					{
						m_tables.emplace(id, std::move(msg));
						msg.clear();
					}
				}
				break;
			default:;
			}
		}

		if (m_tables.empty())
			return;

		for (auto& msg : _messages)
		{
			auto cmd = xt::State::getCommand(msg);

			if (cmd != xt::SysexCommand::SingleDump)
				continue;

			auto table = xt::State::getWavetableFromSingleDump(msg);

			if (xt::wave::isReadOnly(table))
				continue;

			std::vector<xt::SysEx> results;
			getWaveDataForSingle(results, msg);
			if (results.empty())
				continue;

			results.insert(results.begin(), msg);
			auto newSingle = xt::State::createCombinedPatch(results);
			msg.assign(newSingle.begin(), newSingle.end());
		}
	}

	void PatchManager::getWaveDataForSingle(std::vector<xt::SysEx>& _results, const xt::SysEx& _single) const
	{
		const auto tableId = xt::State::getWavetableFromSingleDump(_single);

		if(xt::wave::isReadOnly(tableId))
			return;

		auto itTable = m_tables.find(tableId);
		if (itTable == m_tables.end())
			return;

		xt::TableData table;

		if (!xt::State::parseTableData(table, itTable->second))
			return;

		const auto waves = xt::State::getWavesForTable(table);

		for (const auto waveId : waves)
		{
			if(!xt::wave::isValidWaveIndex(waveId.rawId()))
				continue;

			if(xt::wave::isReadOnly(waveId))
				continue;

			const auto itWave = m_waves.find(waveId);
			if (itWave == m_waves.end())
				continue;

			const auto wave = itWave->second;

			_results.emplace_back(wave);
		}

		_results.emplace_back(itTable->second);
	}

	PatchManager::PatchType PatchManager::detectPatchType(const pluginLib::patchDB::Data& _sysex) const
	{
		if (_sysex.size() < 8)
			return PatchType::Invalid;

		const auto cmd = xt::State::getCommand(_sysex);

		if (cmd == xt::SysexCommand::SingleDump)
			return PatchType::Single;

		if (cmd != xt::SysexCommand::MultiDump)
			return PatchType::Invalid;

		synthLib::SysexBufferList msgs;
		synthLib::MidiToSysex::splitMultipleSysex(msgs, _sysex);

		if (msgs.size() == 1)
			return PatchType::Multi;

		if (msgs.size() == 1 + m_controller.getPartCount()
			&& xt::State::getCommand(msgs.front()) == xt::SysexCommand::MultiDump)
		{
			for (size_t i = 1; i < msgs.size(); ++i)
			{
				if (msgs[i].size() < 8 || xt::State::getCommand(msgs[i]) != xt::SysexCommand::SingleDump)
					return PatchType::Invalid;
			}
			return PatchType::Arrangement;
		}

		return PatchType::Invalid;
	}

	std::string PatchManager::extractMultiName(const pluginLib::patchDB::Data& _sysex)
	{
		constexpr size_t nameOffset = xt::SysexIndex::IdxMultiParamFirst + static_cast<size_t>(xt::MultiParameter::Name00);
		constexpr size_t nameLength = 16;

		if (_sysex.size() < nameOffset + nameLength)
			return {};

		std::string name(reinterpret_cast<const char*>(_sysex.data()) + nameOffset, nameLength);

		while (!name.empty() && (name.back() == ' ' || name.back() == '\0'))
			name.pop_back();

		return name;
	}

	bool PatchManager::activateSingle(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part)
	{
		if(!m_controller.sendSingle(applyModifications(_patch, pluginLib::FileType::Empty, pluginLib::ExportType::EmuHardware), static_cast<uint8_t>(_part)))
		{
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
				m_editor.getProcessor().getProperties().name + " - Unable to load patch",
				"MW1 patches can only be loaded to the first part.\n"
				"\n"
				"If you want to load a MW1 patch to another part, first convert it by loading it to part 1, then save the loaded patch to a user bank.");
		}
		return true;
	}

	bool PatchManager::activateMulti(const pluginLib::patchDB::Data& _multi)
	{
		m_controller.sendMulti(_multi);
		return true;
	}

	bool PatchManager::activateArrangement(const pluginLib::patchDB::Data& _compound)
	{
		synthLib::SysexBufferList msgs;
		synthLib::MidiToSysex::splitMultipleSysex(msgs, _compound);

		if (msgs.size() != 1 + m_controller.getPartCount()
			|| xt::State::getCommand(msgs.front()) != xt::SysexCommand::MultiDump)
			return false;

		m_controller.sendMulti(msgs.front());

		for (uint8_t i = 0; i < m_controller.getPartCount(); ++i)
			m_controller.sendSingle(msgs[i + 1], i);

		return true;
	}
}
