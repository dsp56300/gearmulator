#include "mqPatchManager.h"

#include "mqController.h"
#include "mqEditor.h"
#include "jucePluginEditorLib/pluginProcessor.h"
#include "jucePluginLib/filetype.h"
#include "mqLib/mqstate.h"
#include "mqLib/mqmiditypes.h"
#include "wLib/wMidiTypes.h"

#include "synthLib/midiToSysex.h"

namespace mqJucePlugin
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
		, m_controller(_editor.getMqController())
	{
		addGroupTreeItemForTag(pluginLib::patchDB::TagType::CustomC);
		startLoaderThread();
	}

	PatchManager::~PatchManager()
	{
		stopLoaderThread();
	}

	bool PatchManager::requestPatchForPart(pluginLib::patchDB::Data& _data, uint32_t _part, uint64_t _userData)
	{
		if (_userData == g_userDataArrangement)
		{
			const auto& multiBuf = m_controller.getMultiEditBuffer().data;
			if (multiBuf.empty())
				return false;

			_data.assign(multiBuf.begin(), multiBuf.end());
			for (uint8_t i = 0; i < m_controller.getPartCount(); ++i)
			{
				const auto single = m_controller.createSingleDump(
					mqLib::MidiBufferNum::SingleEditBufferMultiMode,
					mqLib::MidiSoundLocation::EditBufferFirstMultiSingle, i, i);
				_data.insert(_data.end(), single.begin(), single.end());
			}
			return true;
		}

		_data = m_controller.createSingleDump(mqLib::MidiBufferNum::SingleBankA, static_cast<mqLib::MidiSoundLocation>(0), _part, _part);
		return !_data.empty();
	}

	bool PatchManager::loadRomData(pluginLib::patchDB::DataList& _results, uint32_t _bank, uint32_t _program)
	{
		return false;
	}

	PatchManager::PatchType PatchManager::detectPatchType(const pluginLib::patchDB::Data& _sysex) const
	{
		if (_sysex.size() < 8)
			return PatchType::Invalid;

		const auto cmd = static_cast<mqLib::SysexCommand>(_sysex[wLib::IdxCommand]);

		if (cmd == mqLib::SysexCommand::SingleDump)
			return PatchType::Single;

		if (cmd == mqLib::SysexCommand::DrumDump)
			return PatchType::Drum;

		if (cmd != mqLib::SysexCommand::MultiDump)
			return PatchType::Invalid;

		synthLib::SysexBufferList msgs;
		synthLib::MidiToSysex::splitMultipleSysex(msgs, _sysex);

		if (msgs.size() == 1)
			return PatchType::Multi;

		if (msgs.size() == 1 + m_controller.getPartCount()
			&& static_cast<mqLib::SysexCommand>(msgs.front()[wLib::IdxCommand]) == mqLib::SysexCommand::MultiDump)
		{
			for (size_t i = 1; i < msgs.size(); ++i)
			{
				if (msgs[i].size() < 8 || static_cast<mqLib::SysexCommand>(msgs[i][wLib::IdxCommand]) != mqLib::SysexCommand::SingleDump)
					return PatchType::Invalid;
			}
			return PatchType::Arrangement;
		}

		return PatchType::Invalid;
	}

	std::string PatchManager::extractMultiName(const pluginLib::patchDB::Data& _sysex)
	{
		constexpr size_t nameOffset = mqLib::IdxMultiParamFirst + static_cast<size_t>(mqLib::MultiParameter::Name00);
		constexpr size_t nameLength = 16;

		if (_sysex.size() < nameOffset + nameLength)
			return {};

		std::string name(reinterpret_cast<const char*>(_sysex.data()) + nameOffset, nameLength);

		while (!name.empty() && (name.back() == ' ' || name.back() == '\0'))
			name.pop_back();

		return name;
	}

	pluginLib::patchDB::PatchPtr PatchManager::initializePatch(pluginLib::patchDB::Data&& _sysex, const std::string& _defaultPatchName)
	{
		const auto patchType = detectPatchType(_sysex);

		if (patchType == PatchType::Multi || patchType == PatchType::Arrangement || patchType == PatchType::Drum)
		{
			auto patch = std::make_shared<pluginLib::patchDB::Patch>();
			patch->sysex = std::move(_sysex);

			if (patchType == PatchType::Drum)
			{
				patch->name = _defaultPatchName.empty() ? "Drum Map" : _defaultPatchName;
				patch->tags.add(pluginLib::patchDB::TagType::CustomC, "Drum");
			}
			else
			{
				patch->name = extractMultiName(patch->sysex);
				if (patch->name.empty())
					patch->name = _defaultPatchName.empty() ? "Multi" : _defaultPatchName;

				patch->tags.add(pluginLib::patchDB::TagType::CustomC,
					patchType == PatchType::Multi ? "Multi" : "Arrangement");
			}

			return patch;
		}

		if (patchType != PatchType::Single)
			return {};

		pluginLib::MidiPacket::Data data;
		pluginLib::MidiPacket::AnyPartParamValues parameters;
		if(!m_controller.parseSingle(data, parameters, _sysex))
			return {};

		auto p = std::make_shared<pluginLib::patchDB::Patch>();

		p->sysex = std::move(_sysex);
		p->name = m_controller.getSingleName(parameters);
		p->tags.add(pluginLib::patchDB::TagType::CustomC, "Single");

		auto category = m_controller.getCategory(parameters);

		while(!category.empty() && isspace(category.back()))
			category.pop_back();
		while(!category.empty() && isspace(category.front()))
			category.erase(0);

		if(!category.empty())
			p->tags.add(pluginLib::patchDB::TagType::Category, category);

		return p;
	}

	bool PatchManager::parseFileData(pluginLib::patchDB::DataList& _results, const pluginLib::patchDB::Data& _data, const std::string& _filename)
	{
		pluginLib::patchDB::DataList raw;
		if (!pluginLib::patchDB::DB::parseFileData(raw, _data, _filename))
			return false;

		auto isSingle = [](const pluginLib::patchDB::Data& d)
		{
			return d.size() >= 8 && static_cast<mqLib::SysexCommand>(d[wLib::IdxCommand]) == mqLib::SysexCommand::SingleDump;
		};
		auto isMulti = [](const pluginLib::patchDB::Data& d)
		{
			return d.size() >= 8 && static_cast<mqLib::SysexCommand>(d[wLib::IdxCommand]) == mqLib::SysexCommand::MultiDump;
		};

		for (size_t i = 0; i < raw.size();)
		{
			if (isMulti(raw[i]) && i + m_controller.getPartCount() < raw.size())
			{
				bool allSingles = true;
				for (size_t j = 1; j <= m_controller.getPartCount(); ++j)
				{
					if (!isSingle(raw[i + j]))
					{
						allSingles = false;
						break;
					}
				}
				if (allSingles)
				{
					pluginLib::patchDB::Data compound = raw[i];
					for (size_t j = 1; j <= m_controller.getPartCount(); ++j)
						compound.insert(compound.end(), raw[i + j].begin(), raw[i + j].end());
					_results.emplace_back(std::move(compound));
					i += 1 + m_controller.getPartCount();
					continue;
				}
			}
			_results.emplace_back(std::move(raw[i]));
			++i;
		}

		return !_results.empty();
	}

	pluginLib::patchDB::Data PatchManager::applyModifications(const pluginLib::patchDB::PatchPtr& _patch, const pluginLib::FileType& _fileType, pluginLib::ExportType _exportType) const
	{
		const auto patchType = detectPatchType(_patch->sysex);

		if (patchType == PatchType::Multi || patchType == PatchType::Drum)
		{
			auto result = _patch->sysex;
			mqLib::State::updateChecksum(result);
			return result;
		}

		if (patchType == PatchType::Arrangement)
		{
			synthLib::SysexBufferList msgs;
			synthLib::MidiToSysex::splitMultipleSysex(msgs, _patch->sysex);

			if (msgs.size() != 1 + m_controller.getPartCount())
				return _patch->sysex;

			mqLib::State::updateChecksum(msgs[0]);
			for (size_t i = 1; i < msgs.size(); ++i)
				mqLib::State::updateChecksum(msgs[i]);

			pluginLib::patchDB::Data result;
			for (auto& msg : msgs)
				result.insert(result.end(), msg.begin(), msg.end());
			return result;
		}

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
		const auto sysex = applyModifications(_patch, pluginLib::FileType::Empty, pluginLib::ExportType::EmuHardware);
		const auto type = detectPatchType(sysex);

		switch (type)
		{
		case PatchType::Single:
			return activateSingle(sysex, _part);
		case PatchType::Multi:
			return activateMulti(sysex);
		case PatchType::Drum:
			return activateDrum(sysex);
		case PatchType::Arrangement:
			return activateArrangement(sysex);
		default:
			return false;
		}
	}

	bool PatchManager::activateSingle(const pluginLib::patchDB::Data& _sysex, uint32_t _part)
	{
		m_controller.sendSingle(_sysex, static_cast<uint8_t>(_part));
		return true;
	}

	bool PatchManager::activateMulti(const pluginLib::patchDB::Data& _multi)
	{
		m_controller.sendMulti(_multi);
		return true;
	}

	bool PatchManager::activateDrum(const pluginLib::patchDB::Data& _drum)
	{
		m_controller.sendDrum(_drum);
		return true;
	}

	bool PatchManager::activateArrangement(const pluginLib::patchDB::Data& _compound)
	{
		synthLib::SysexBufferList msgs;
		synthLib::MidiToSysex::splitMultipleSysex(msgs, _compound);

		if (msgs.size() != 1 + m_controller.getPartCount()
			|| static_cast<mqLib::SysexCommand>(msgs.front()[wLib::IdxCommand]) != mqLib::SysexCommand::MultiDump)
			return false;

		m_controller.sendMulti(msgs.front());

		for (uint8_t i = 0; i < m_controller.getPartCount(); ++i)
			m_controller.sendSingle(msgs[i + 1], i);

		return true;
	}
}
