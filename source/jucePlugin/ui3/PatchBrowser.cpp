#include "PatchBrowser.h"

#include "VirusEditor.h"
#include "../PluginProcessor.h"

#include "../../virusLib/microcontrollerTypes.h"
#include "../../virusLib/microcontroller.h"

#include "../VirusController.h"

#include "../../synthLib/midiToSysex.h"
#include "../../synthLib/os.h"

using namespace juce;

namespace genericVirusUI
{
    enum Columns
	{
        INDEX = 1,
        NAME = 2,
        CAT1 = 3,
        CAT2 = 4,
        ARP = 5,
        UNI = 6,
        ST = 7,
        VER = 8,
	};

	constexpr std::initializer_list<jucePluginEditorLib::PatchBrowser::ColumnDefinition> g_columns =
	{
		{"#", INDEX, 32},
		{"Name", NAME, 130},
		{"Category1", CAT1, 84},
		{"Category2", CAT2, 84},
		{"Arp", ARP, 32},
		{"Uni", UNI, 32},
		{"ST+-", ST, 32},
		{"Ver", VER, 32}
	};

	PatchBrowser::PatchBrowser(const VirusEditor& _editor) : jucePluginEditorLib::PatchBrowser(_editor, _editor.getController(), _editor.getProcessor().getConfig(), g_columns)
	{
		const auto& c = _editor.getController();

		if(m_romBankSelect)
		{
			int id=1;

			m_romBankSelect->addItem("-", 1);

			for(uint32_t i=0; i<c.getBankCount(); ++i)
			{
				m_romBankSelect->addItem(c.getBankName(i), ++id);
			}

			m_romBankSelect->onChange = [this]
			{
				const auto index = m_romBankSelect->getSelectedItemIndex();
				if(index > 0)
					loadRomBank(index - 1);
			};
		}
	}

	void PatchBrowser::loadRomBank(const uint32_t _bankIndex)
	{
		const auto& singles = static_cast<Virus::Controller&>(m_controller).getSinglePresets();

		if(_bankIndex >= singles.size())
			return;

		const auto& bank = singles[_bankIndex];

		const auto searchValue = m_search.getText();

		PatchList patches;

		for(size_t s=0; s<bank.size(); ++s)
		{
			auto* patch = createPatch();

			patch->sysex = bank[s].data;
			patch->progNumber = static_cast<int>(s);

			if(!initializePatch(*patch))
				continue;

			patches.push_back(std::shared_ptr<jucePluginEditorLib::Patch>(patch));
		}

		fillPatchList(patches);
	}

	bool PatchBrowser::selectPrevNextPreset(int _dir)
	{
		const auto part = m_controller.getCurrentPart();

		const auto& c = static_cast<const Virus::Controller&>(m_controller);

		if(c.getCurrentPartPresetSource(part) == Virus::Controller::PresetSource::Rom)
			return false;

		if(m_filteredPatches.empty())
			return false;

		const auto idx = m_patchList.getSelectedRow();
		if(idx < 0)
			return false;

		const auto name = c.getCurrentPartPresetName(part);

		if(m_filteredPatches[idx]->name != name)
			return false;

		return jucePluginEditorLib::PatchBrowser::selectPrevNextPreset(_dir);
	}

	bool PatchBrowser::initializePatch(jucePluginEditorLib::Patch& _patch)
	{
		if (_patch.sysex.size() < 267)
			return false;

		const auto& c = static_cast<const Virus::Controller&>(m_controller);
		auto& patch = static_cast<Patch&>(_patch);

		pluginLib::MidiPacket::Data data;
		pluginLib::MidiPacket::ParamValues parameterValues;

		if(!c.parseSingle(data, parameterValues, patch.sysex))
			return false;

		const auto idxVersion = c.getParameterIndexByName("Version");
		const auto idxCategory1 = c.getParameterIndexByName("Category1");
		const auto idxCategory2 = c.getParameterIndexByName("Category2");
		const auto idxUnison = c.getParameterIndexByName("Unison Mode");
		const auto idxTranspose = c.getParameterIndexByName("Transpose");
		const auto idxArpMode = c.getParameterIndexByName("Arp Mode");

		patch.name = c.getSinglePresetName(parameterValues);
		patch.model = virusLib::Microcontroller::getPresetVersion(parameterValues.find(std::make_pair(pluginLib::MidiPacket::AnyPart, idxVersion))->second);
		patch.unison = parameterValues.find(std::make_pair(pluginLib::MidiPacket::AnyPart, idxUnison))->second;
		patch.transpose = parameterValues.find(std::make_pair(pluginLib::MidiPacket::AnyPart, idxTranspose))->second;
		patch.arpMode = parameterValues.find(std::make_pair(pluginLib::MidiPacket::AnyPart, idxArpMode))->second;

		const auto category1 = parameterValues.find(std::make_pair(pluginLib::MidiPacket::AnyPart, idxCategory1))->second;
		const auto category2 = parameterValues.find(std::make_pair(pluginLib::MidiPacket::AnyPart, idxCategory2))->second;

		const auto* paramCategory1 = c.getParameter(idxCategory1, 0);
		const auto* paramCategory2 = c.getParameter(idxCategory2, 0);
		
		patch.category1 = paramCategory1->getDescription().valueList.valueToText(category1);
		patch.category2 = paramCategory2->getDescription().valueList.valueToText(category2);

		return true;
	}

	MD5 PatchBrowser::getChecksum(jucePluginEditorLib::Patch& _patch)
	{
		return {&_patch.sysex.front() + 9 + 17, 256 - 17 - 3};
	}

	bool PatchBrowser::activatePatch(jucePluginEditorLib::Patch& _patch)
	{
		auto& c = static_cast<Virus::Controller&>(m_controller);

		// re-pack single, force to edit buffer
		const auto program = c.isMultiMode() ? c.getCurrentPart() : static_cast<uint8_t>(virusLib::ProgramType::SINGLE);

		const auto msg = c.modifySingleDump(_patch.sysex, virusLib::BankNumber::EditBuffer, program, true, true);

		if(msg.empty())
			return false;

		c.sendSysEx(msg);
		c.requestSingle(0x0, program);

		c.setCurrentPartPresetSource(m_controller.getCurrentPart(), Virus::Controller::PresetSource::Browser);

		return true;
	}

	int PatchBrowser::comparePatches(const int _columnId, const jucePluginEditorLib::Patch& _a, const jucePluginEditorLib::Patch& _b) const
	{
		const auto& a = static_cast<const Patch&>(_a);
		const auto& b = static_cast<const Patch&>(_b);

		switch(_columnId)
		{
		case INDEX:	return a.progNumber - b.progNumber;
		case NAME:	return String(a.name).compareIgnoreCase(b.name);
		case CAT1:	return a.category1.compare(b.category1);
		case CAT2:	return a.category2.compare(b.category2);
		case ARP:	return a.arpMode - b.arpMode;
		case UNI:	return a.unison - b.unison;
		case VER:	return a.model - b.model;
		case ST:	return a.transpose - b.transpose;
		default:	return a.progNumber - b.progNumber;
		}
	}

	bool PatchBrowser::loadUnkownData(std::vector<std::vector<uint8_t>>& _result, const std::string& _filename)
	{
		std::vector<uint8_t> data;

		if(!synthLib::readFile(data, _filename))
			return jucePluginEditorLib::PatchBrowser::loadUnkownData(_result, _filename);

		if(virusLib::Device::parsePowercorePreset(_result, data))
			return true;

		return jucePluginEditorLib::PatchBrowser::loadUnkownData(_result, _filename);
	}

	std::string PatchBrowser::getCellText(const jucePluginEditorLib::Patch& _patch, int columnId)
	{
		auto& rowElement = static_cast<const Patch&>(_patch);

		switch (columnId)
		{
		case INDEX:		return std::to_string(rowElement.progNumber);
		case NAME:		return rowElement.name;
		case CAT1:		return rowElement.category1;
		case CAT2:		return rowElement.category2;
		case ARP:		return rowElement.arpMode != 0 ? "Y" : " ";
		case UNI:		return rowElement.unison == 0 ? " " : std::to_string(rowElement.unison + 1);
		case ST:		return rowElement.transpose != 64 ? std::to_string(rowElement.transpose - 64) : " ";
		case VER:
			{
				switch (rowElement.model)
				{
				case virusLib::A:	return "A";
				case virusLib::B:	return "B";
				case virusLib::C:	return "C";
				case virusLib::D:	return "TI";
				case virusLib::D2:  return "TI2";
				default:			return "?";
				}
			}
		default:		return "?";
		}
	}
}
