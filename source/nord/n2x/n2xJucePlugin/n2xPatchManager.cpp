#include "n2xPatchManager.h"

#include "n2xController.h"
#include "n2xEditor.h"

#include "juce_cryptography/hashing/juce_MD5.h"

#include "n2xLib/n2xmiditypes.h"

namespace n2xJucePlugin
{
	constexpr char g_performancePrefixes[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'J', 'L' };

	static constexpr std::initializer_list<jucePluginEditorLib::patchManager::GroupType> g_groupTypes =
	{
		jucePluginEditorLib::patchManager::GroupType::Favourites,
		jucePluginEditorLib::patchManager::GroupType::LocalStorage,
		jucePluginEditorLib::patchManager::GroupType::DataSources,
	};

	PatchManager::PatchManager(Editor& _editor, Component* _root)
	: jucePluginEditorLib::patchManager::PatchManager(_editor, _root, g_groupTypes)
	, m_editor(_editor)
	, m_controller(_editor.getN2xController())
	{
		setTagTypeName(pluginLib::patchDB::TagType::CustomA, "Patch Type");
		startLoaderThread();
		addGroupTreeItemForTag(pluginLib::patchDB::TagType::CustomA);
	}

	PatchManager::~PatchManager()
	{
		stopLoaderThread();
	}

	bool PatchManager::requestPatchForPart(pluginLib::patchDB::Data& _data, const uint32_t _part, const uint64_t _userData)
	{
		if(_userData)
			_data = m_controller.createMultiDump(n2x::SysexByte::MultiDumpBankA, 0);
		else
			_data = m_controller.createSingleDump(n2x::SysexByte::SingleDumpBankA, 0, static_cast<uint8_t>(_part));
		return !_data.empty();
	}

	bool PatchManager::loadRomData(pluginLib::patchDB::DataList& _results, uint32_t _bank, uint32_t _program)
	{
		return false;
	}

	pluginLib::patchDB::PatchPtr PatchManager::initializePatch(pluginLib::patchDB::Data&& _sysex)
	{
		if(!isValidPatchDump(_sysex))
			return {};

		const auto bank = _sysex[n2x::SysexIndex::IdxMsgType];
		const auto program = _sysex[n2x::SysexIndex::IdxMsgSpec];
		const auto isSingle = n2x::State::isSingleDump(_sysex);

		auto p = std::make_shared<pluginLib::patchDB::Patch>();

		p->tags.add(pluginLib::patchDB::TagType::CustomA, isSingle ? "Program" : "Performance");

		if(isSingle)
		{
			const auto distRmSync = n2x::State::getSingleParam(_sysex, n2x::SingleParam::Distortion, 0);
			if(distRmSync & 1)
				p->tags.add(pluginLib::patchDB::TagType::Tag, "Sync");
			if(distRmSync & 2)
				p->tags.add(pluginLib::patchDB::TagType::Tag, "RingMod");
			if(distRmSync & (1<<4))
				p->tags.add(pluginLib::patchDB::TagType::Tag, "Distortion");

			if(n2x::State::getSingleParam(_sysex, n2x::SingleParam::Unison, 0))
				p->tags.add(pluginLib::patchDB::TagType::Tag, "Unison");

			const auto voiceMode = n2x::State::getSingleParam(_sysex, n2x::SingleParam::VoiceMode, 0);
			if(voiceMode == 2)
				p->tags.add(pluginLib::patchDB::TagType::Tag, "Poly");
			else if(voiceMode == 1)
				p->tags.add(pluginLib::patchDB::TagType::Tag, "Legato");
			else
				p->tags.add(pluginLib::patchDB::TagType::Tag, "Mono");
		}

		p->name = getPatchName(_sysex);
		p->sysex = std::move(_sysex);
		p->program = program;
		p->bank = bank;

		const juce::MD5 md5(p->sysex.data() + n2x::g_sysexHeaderSize, p->sysex.size() - n2x::g_sysexContainerSize);
		static_assert(sizeof(juce::MD5) >= sizeof(pluginLib::patchDB::PatchHash));
		memcpy(p->hash.data(), md5.getChecksumDataArray(), std::size(p->hash));

		return p;
	}

	pluginLib::patchDB::Data PatchManager::prepareSave(const pluginLib::patchDB::PatchPtr& _patch) const
	{
		auto d = n2x::State::stripPatchName(_patch->sysex);

		d[n2x::SysexIndex::IdxMsgType] = static_cast<uint8_t>(_patch->bank);
		d[n2x::SysexIndex::IdxMsgSpec] = static_cast<uint8_t>(_patch->program);

		auto name = _patch->getName();

		if(name.size() > n2x::g_nameLength)
			name = name.substr(0, n2x::g_nameLength);
		while(name.size() < n2x::g_nameLength)
			name.push_back(' ');

		d.pop_back();
		d.insert(d.end(), name.begin(), name.end());
		d.push_back(0xf7);

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

	bool PatchManager::activatePatch(const pluginLib::patchDB::PatchPtr& _patch, const uint32_t _part)
	{
		if(!m_controller.activatePatch(_patch->sysex, _part))
			return false;

		m_editor.onPatchActivated(_patch, _part);
		return true;
	}

	bool PatchManager::parseFileData(pluginLib::patchDB::DataList& _results, const pluginLib::patchDB::Data& _data)
	{
		return jucePluginEditorLib::patchManager::PatchManager::parseFileData(_results, _data);
	}

	std::string PatchManager::getPatchName(const pluginLib::patchDB::Data& _sysex)
	{
		if(!isValidPatchDump(_sysex))
			return {};

		{
			const auto nameFromDump = n2x::State::extractPatchName(_sysex);
			if(!nameFromDump.empty())
				return nameFromDump;
		}

		const auto isSingle = n2x::State::isSingleDump(_sysex);

		const auto bank = _sysex[n2x::SysexIndex::IdxMsgType];
		const auto program = _sysex[n2x::SysexIndex::IdxMsgSpec];

		char name[128]{0};

		auto getBankChar = [&]() -> char
		{
			if(isSingle)
			{
				if(bank == n2x::SingleDumpBankEditBuffer)
					return 'e';
				return static_cast<char>('0' + bank - n2x::SysexByte::SingleDumpBankA);
			}
			if(bank == n2x::MultiDumpBankEditBuffer)
				return 'e';

			return static_cast<char>('0' + bank - n2x::SysexByte::MultiDumpBankA);
		};

		if(isSingle)
		{
			(void)snprintf(name, sizeof(name), "%c.%02d", getBankChar(), program);
		}
		else
		{
			(void)snprintf(name, sizeof(name), "%c.%c%01d", getBankChar(), g_performancePrefixes[(program/10)%std::size(g_performancePrefixes)], program % 10);
		}

		return name;
	}

	bool PatchManager::isValidPatchDump(const pluginLib::patchDB::Data& _sysex)
	{
		const auto isSingle = n2x::State::isSingleDump(_sysex);
		const auto isMulti = n2x::State::isMultiDump(_sysex);

		if(!isSingle && !isMulti)
			return false;

		const auto deviceId = _sysex[n2x::SysexIndex::IdxDevice];
		const auto bank = _sysex[n2x::SysexIndex::IdxMsgType];
		const auto program = _sysex[n2x::SysexIndex::IdxMsgSpec];

		if(deviceId > 15)
			return false;

		if(program > n2x::g_programsPerBank)
			return false;

		if(isSingle && (bank < n2x::SysexByte::SingleDumpBankEditBuffer || bank > (n2x::SysexByte::SingleDumpBankEditBuffer + n2x::g_singleBankCount)))
			return false;

		if(isMulti && (bank < n2x::SysexByte::MultiDumpBankEditBuffer || bank > (n2x::SysexByte::MultiDumpBankEditBuffer + n2x::g_multiBankCount)))
			return false;

		return true;
	}
}
