#include "PatchManager.h"

#include "VirusEditor.h"
#include "../VirusController.h"

#include "../../jucePluginLib/patchdb/datasource.h"
#include "../../jucePluginEditorLib/pluginEditor.h"

#include "../../virusLib/microcontroller.h"
#include "../../virusLib/device.h"

namespace Virus
{
	class Controller;
}

namespace genericVirusUI
{
	pluginLib::patchDB::DataSource createRomDataSource(const uint32_t _bank)
	{
		pluginLib::patchDB::DataSource ds;
		ds.type = pluginLib::patchDB::SourceType::Rom;
		ds.bank = _bank;
		char temp[32];
		snprintf(temp, sizeof(temp), "ROM %c", 'A' + _bank);
		ds.name = temp;
		return ds;
	}

	void PatchManager::selectRomPreset(uint32_t _part, virusLib::BankNumber _bank, uint8_t _program)
	{
		const pluginLib::patchDB::DataSource ds = createRomDataSource(toArrayIndex(_bank));

		selectPatch(_part, ds, _program);
	}

	PatchManager::PatchManager(VirusEditor& _editor, juce::Component* _root, const juce::File& _dir) : jucePluginEditorLib::patchManager::PatchManager(_editor, _root, _dir), m_controller(_editor.getController())
	{
		addRomPatches();

		startLoaderThread();

		// rom patches are received via midi, make sure we add all remaining ones, too
		m_controller.onRomPatchReceived = [this](const virusLib::BankNumber _bank, const uint32_t _program)
		{
			if (_bank == virusLib::BankNumber::EditBuffer || _bank < virusLib::BankNumber::C)
				return;

			const auto index = virusLib::toArrayIndex(_bank);

			const auto& banks = m_controller.getSinglePresets();

			if(index < banks.size())
			{
				const auto& bank = banks[index];

				if(_program == bank.size() - 1)
					addDataSource(createRomDataSource(index));
			}
		};
	}

	PatchManager::~PatchManager()
	{
		stopLoaderThread();
		m_controller.onRomPatchReceived = {};
	}

	bool PatchManager::loadRomData(pluginLib::patchDB::DataList& _results, const uint32_t _bank, const uint32_t _program)
	{
		const auto bankIndex = _bank;

		const auto& singles = m_controller.getSinglePresets();

		if (bankIndex >= singles.size())
			return false;

		const auto& bank = singles[bankIndex];

		if(_program != pluginLib::patchDB::g_invalidProgram)
		{
			if (_program >= bank.size())
				return false;
			const auto& s = bank[_program];
			if (s.data.empty())
				return false;
			_results.push_back(s.data);
		}
		else
		{
			_results.reserve(bank.size());
			for (const auto& patch : bank)
				_results.push_back(patch.data);
		}
		return true;
	}

	std::shared_ptr<pluginLib::patchDB::Patch> PatchManager::initializePatch(const std::vector<uint8_t>& _sysex)
	{
		if (_sysex.size() < 267)
			return nullptr;

		const auto& c = static_cast<const Virus::Controller&>(m_controller);

		pluginLib::MidiPacket::Data data;
		pluginLib::MidiPacket::ParamValues parameterValues;

		if (!c.parseSingle(data, parameterValues, _sysex))
			return nullptr;

		const auto idxVersion = c.getParameterIndexByName("Version");
		const auto idxCategory1 = c.getParameterIndexByName("Category1");
		const auto idxCategory2 = c.getParameterIndexByName("Category2");
		const auto idxUnison = c.getParameterIndexByName("Unison Mode");
		const auto idxTranspose = c.getParameterIndexByName("Transpose");
		const auto idxArpMode = c.getParameterIndexByName("Arp Mode");

		auto patch = std::make_shared<Patch>();

		{
			const auto it = data.find(pluginLib::MidiDataType::Bank);
			if (it != data.end())
				patch->bank = it->second;
		}
		{
			const auto it = data.find(pluginLib::MidiDataType::Program);
			if (it != data.end())
				patch->program = it->second;
		}

		patch->sysex = _sysex;

		{
			constexpr auto frontOffset = 9;			// remove bank number, program number and other stuff that we don't need, first index is the patch version
			constexpr auto backOffset = 2;			// remove f7 and checksum
			const juce::MD5 md5(_sysex.data() + frontOffset, _sysex.size() - frontOffset - backOffset);

			static_assert(sizeof(juce::MD5) >= sizeof(pluginLib::patchDB::PatchHash));
			memcpy(patch->hash.data(), md5.getChecksumDataArray(), std::size(patch->hash));
		}

		patch->name = c.getSinglePresetName(parameterValues);
		patch->version = virusLib::Microcontroller::getPresetVersion(parameterValues.find(std::make_pair(pluginLib::MidiPacket::AnyPart, idxVersion))->second);
		patch->unison = parameterValues.find(std::make_pair(pluginLib::MidiPacket::AnyPart, idxUnison))->second;
		patch->transpose = parameterValues.find(std::make_pair(pluginLib::MidiPacket::AnyPart, idxTranspose))->second;
		patch->arpMode = parameterValues.find(std::make_pair(pluginLib::MidiPacket::AnyPart, idxArpMode))->second;

		const auto category1 = parameterValues.find(std::make_pair(pluginLib::MidiPacket::AnyPart, idxCategory1))->second;
		const auto category2 = parameterValues.find(std::make_pair(pluginLib::MidiPacket::AnyPart, idxCategory2))->second;

		const auto* paramCategory1 = c.getParameter(idxCategory1, 0);
		const auto* paramCategory2 = c.getParameter(idxCategory2, 0);

		if (category1)
			patch->tags.add(pluginLib::patchDB::TagType::Category, paramCategory1->getDescription().valueList.valueToText(category1));
		if (category2)
			patch->tags.add(pluginLib::patchDB::TagType::Category, paramCategory2->getDescription().valueList.valueToText(category2));

		switch (patch->version)
		{
		case virusLib::A:	patch->tags.add(pluginLib::patchDB::TagType::CustomA, "A");		break;
		case virusLib::B:	patch->tags.add(pluginLib::patchDB::TagType::CustomA, "B");		break;
		case virusLib::C:	patch->tags.add(pluginLib::patchDB::TagType::CustomA, "C");		break;
		case virusLib::D:	patch->tags.add(pluginLib::patchDB::TagType::CustomA, "TI");		break;
		case virusLib::D2:	patch->tags.add(pluginLib::patchDB::TagType::CustomA, "TI2");		break;
		}
		return patch;
	}

	pluginLib::patchDB::Data PatchManager::prepareSave(const pluginLib::patchDB::PatchPtr& _patch) const
	{
		pluginLib::MidiPacket::Data data;
		pluginLib::MidiPacket::ParamValues parameterValues;

		if (!m_controller.parseSingle(data, parameterValues, _patch->sysex))
			return _patch->sysex;

		// apply name
		if (!_patch->name.empty())
			m_controller.setSinglePresetName(parameterValues, _patch->name);

		// apply program
		auto program = data[pluginLib::MidiDataType::Program];

		if (_patch->program != pluginLib::patchDB::g_invalidProgram)
		{
			program = _patch->program <= 127 ? static_cast<uint8_t>(_patch->program) : 127;
		}

		// apply categories
		const uint32_t indicesCategory[] = {
			m_controller.getParameterIndexByName("Category1"),
			m_controller.getParameterIndexByName("Category2")
		};

		const pluginLib::Parameter* paramsCategory[] = {
			m_controller.getParameter(indicesCategory[0], 0),
			m_controller.getParameter(indicesCategory[1], 0)
		};

		auto& val0 = parameterValues.find(std::make_pair(pluginLib::MidiPacket::AnyPart, indicesCategory[0]))->second;
		auto& val1 = parameterValues.find(std::make_pair(pluginLib::MidiPacket::AnyPart, indicesCategory[1]))->second;

		val0 = val1 = 0;

		const auto& tags = _patch->getTags(pluginLib::patchDB::TagType::Category);

		size_t i = 0;
		for (const auto& tag : tags.getAdded())
		{
			const auto categoryValue = paramsCategory[i]->getDescription().valueList.textToValue(tag);
			if(categoryValue != 0)
			{
				auto& v = i ? val1 : val0;
				v = static_cast<uint8_t>(categoryValue);
				++i;
				if (i == 2)
					break;
			}
		}

		return m_controller.createSingleDump(toMidiByte(virusLib::BankNumber::A), program, parameterValues);
	}

	bool PatchManager::parseFileData(pluginLib::patchDB::DataList& _results, const pluginLib::patchDB::Data& _data)
	{
		{
			std::vector<synthLib::SMidiEvent> events;
			virusLib::Device::parseTIcontrolPreset(events, _data);

			for (const auto& e : events)
			{
				if (!e.sysex.empty())
					_results.push_back(e.sysex);
			}

			if (!_results.empty())
				return true;
		}

		if (virusLib::Device::parsePowercorePreset(_results, _data))
			return true;

		return jucePluginEditorLib::patchManager::PatchManager::parseFileData(_results, _data);
	}

	bool PatchManager::requestPatchForPart(pluginLib::patchDB::Data& _data, const uint32_t _part)
	{
		_data = m_controller.createSingleDump(static_cast<uint8_t>(_part), toMidiByte(virusLib::BankNumber::A), 0);
		return !_data.empty();
	}

	uint32_t PatchManager::getCurrentPart() const
	{
		return m_controller.getCurrentPart();
	}

	bool PatchManager::equals(const pluginLib::patchDB::PatchPtr& _a, const pluginLib::patchDB::PatchPtr& _b) const
	{
		pluginLib::MidiPacket::Data dataA, dataB;
		pluginLib::MidiPacket::ParamValues parameterValuesA, parameterValuesB;

		if (!m_controller.parseSingle(dataA, parameterValuesA, _a->sysex) || !m_controller.parseSingle(dataB, parameterValuesB, _b->sysex))
			return false;

		if(parameterValuesA.size() != parameterValuesB.size())
			return false;

		for (const auto& itA : parameterValuesA)
		{
			const auto& k = itA.first;
			auto vA = itA.second;

			const auto& itB = parameterValuesB.find(k);

			if(itB == parameterValuesB.end())
				return false;

			auto vB = itB->second;

			if(vA != vB)
			{
				// parameters might be out of range because some dumps have values that are out of range indeed, clamp to valid range and compare again
				const auto* param = m_controller.getParameter(itB->first.second);
				if(!param)
					return false;

				if(param->getDescription().isNonPartSensitive())
					continue;

				vA = static_cast<uint8_t>(param->getDescription().range.clipValue(vA));
				vB = static_cast<uint8_t>(param->getDescription().range.clipValue(vB));

				if(vA != vB)
					return false;
			}
		}
		return true;
	}

	bool PatchManager::activatePatch(const pluginLib::patchDB::PatchPtr& _patch)
	{
		return m_controller.activatePatch(_patch->sysex);
	}

	bool PatchManager::activatePatch(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part)
	{
		return m_controller.activatePatch(_patch->sysex, _part);
	}

	void PatchManager::addRomPatches()
	{
		const auto& singles = m_controller.getSinglePresets();

		for (uint32_t b = 0; b < singles.size(); ++b)
		{
			const auto& bank = singles[b];

			const auto& single = bank[bank.size()-1];

			if (single.data.empty())
				continue;

			addDataSource(createRomDataSource(b));
		}
	}
}
