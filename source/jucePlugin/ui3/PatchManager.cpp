#include "PatchManager.h"

#include "../VirusController.h"

#include "../../jucePluginLib/patchdb/datasource.h"

#include "../../virusLib/microcontroller.h"
#include "juce_cryptography/hashing/juce_MD5.h"

namespace Virus
{
	class Controller;
}

namespace genericVirusUI
{
	const auto g_firstRomBankIndex = toArrayIndex(virusLib::BankNumber::C);

	constexpr const char* const g_categories[] = 
	{
		"Off", "Lead", "Bass", "Pad", "Decay", "Pluck", "Acid", "Classic",
		"Arpeggiator", "Effects", "Drums", "Percussion", "Input", "Vocoder",
		"Favourite 1", "Favourite 2", "Favourite 3",
		"Organ", "Piano", "String", "FM", "Digital", "Atomizer"
	};

	pluginLib::patchDB::DataSource createRomDataSource(const uint32_t _bank, const uint32_t _program)
	{
		pluginLib::patchDB::DataSource ds;
		ds.type = pluginLib::patchDB::SourceType::Rom;
		ds.bank = _bank;
		ds.program = _program;
		char temp[32];
		snprintf(temp, sizeof(temp), "ROM %c", 'C' + _bank);
		ds.name = temp;
		return ds;
	}

	PatchManager::PatchManager(Virus::Controller& _controller, juce::Component* _root) : jucePluginEditorLib::PatchManager(_root), m_controller(_controller)
	{
		for (const auto& category : g_categories)
			addCategory(category);

		addRomPatches();

		// rom patches are received via midi, make sure we add all remaining ones, too
		_controller.onRomPatchReceived = [this](const virusLib::BankNumber _bank, const uint32_t _program)
		{
			if (_bank == virusLib::BankNumber::EditBuffer || _bank < virusLib::BankNumber::C)
				return;
			const auto index = virusLib::toArrayIndex(_bank) - g_firstRomBankIndex;
			addDataSource(createRomDataSource(index, _program));
		};
	}

	PatchManager::~PatchManager()
	{
		m_controller.onRomPatchReceived = {};
	}

	bool PatchManager::loadRomData(std::vector<uint8_t>& _result, const uint32_t _bank, const uint32_t _program)
	{
		const auto bankIndex = _bank + g_firstRomBankIndex;
		const auto& singles = m_controller.getSinglePresets();
		if (bankIndex >= singles.size())
			return false;
		const auto& bank = singles[bankIndex];
		if (_program >= bank.size())
			return false;
		const auto& s = bank[_program];
		if (s.data.empty())
			return false;
		_result = s.data;
		return true;
	}

	std::shared_ptr<pluginLib::patchDB::Patch> PatchManager::initializePatch(const std::vector<uint8_t>& _sysex, const pluginLib::patchDB::DataSource& _ds)
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

		patch->sysex = _sysex;
		patch->source = _ds;

		{
			const auto frontOffset = idxVersion;	// remove bank number, program number and other stuff that we don't need
			constexpr auto backOffset = 2;			// remove f7 and checksum
			const juce::MD5 md5(_sysex.data() + frontOffset, _sysex.size() - frontOffset - backOffset);
			patch->hash = md5.toHexString().toStdString();
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
			patch->categories.push_back(paramCategory1->getDescription().valueList.valueToText(category1));
		if (category2)
			patch->categories.push_back(paramCategory2->getDescription().valueList.valueToText(category2));

		return patch;
	}

	void PatchManager::addRomPatches()
	{
		const auto& singles = m_controller.getSinglePresets();

		for (uint32_t b = 0; b < singles.size(); ++b)
		{
			const auto& bank = singles[b];

			for (uint32_t p = 0; p < bank.size(); ++p)
			{
				const auto& single = bank[p];

				if (single.data.empty())
					continue;

				addDataSource(createRomDataSource(b, p));
			}
		}
	}
}
