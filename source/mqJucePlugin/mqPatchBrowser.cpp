#include "mqPatchBrowser.h"

#include "mqController.h"

#include "../mqLib/mqstate.h"

namespace mqJucePlugin
{
    enum Columns
	{
        INDEX = 1,
        NAME = 2,
        CAT = 3,
	};

	constexpr std::initializer_list<jucePluginEditorLib::PatchBrowser::ColumnDefinition> g_columns =
	{
		{"#", INDEX, 32},
		{"Name", NAME, 150},
		{"Category", CAT, 70}
	};

	PatchBrowser::PatchBrowser(const jucePluginEditorLib::Editor& _editor, pluginLib::Controller& _controller, juce::PropertiesFile& _config) : jucePluginEditorLib::PatchBrowser(_editor, _controller, _config, g_columns)
	{
	}

	jucePluginEditorLib::Patch* PatchBrowser::createPatch()
	{
		return new Patch();
	}

	bool PatchBrowser::initializePatch(jucePluginEditorLib::Patch& _patch)
	{
		auto& patch = static_cast<Patch&>(_patch);

		const auto& dumpMq = mqLib::State::g_dumps[static_cast<uint32_t>(mqLib::State::DumpType::Single)];

		if (patch.sysex.size() == dumpMq.dumpSize)
		{
			patch.name = std::string(reinterpret_cast<const char *>(&patch.sysex[dumpMq.firstParamIndex + 363]), 16);
			patch.category = std::string(reinterpret_cast<const char *>(&patch.sysex[dumpMq.firstParamIndex + 379]), 4);

			return true;
		}

		const auto &dumpQ = mqLib::State::g_dumps[static_cast<uint32_t>(mqLib::State::DumpType::SingleQ)];

		if (patch.sysex.size() == dumpQ.dumpSize)
		{
			patch.name = std::string(reinterpret_cast<const char *>(&patch.sysex[dumpQ.firstParamIndex + 364]), 16);
			patch.category = std::string(reinterpret_cast<const char *>(&patch.sysex[dumpQ.firstParamIndex + 380]), 4);

			return true;
		}
		return false;
	}

	juce::MD5 PatchBrowser::getChecksum(jucePluginEditorLib::Patch& _patch)
	{
		const auto& dump = mqLib::State::g_dumps[static_cast<uint32_t>(mqLib::State::DumpType::Single)];

		return {&_patch.sysex[dump.firstParamIndex], dump.dumpSize - 2 - dump.firstParamIndex};
	}

	bool PatchBrowser::activatePatch(jucePluginEditorLib::Patch& _patch)
	{
		auto& c = static_cast<Controller&>(m_controller);

		c.sendSingle(_patch.sysex);

		return true;
	}

	int PatchBrowser::comparePatches(int _columnId, const jucePluginEditorLib::Patch& _a, const jucePluginEditorLib::Patch& _b) const
	{
		auto& a = static_cast<const Patch&>(_a);
		auto& b = static_cast<const Patch&>(_b);

		switch(_columnId)
		{
			case INDEX:		return a.progNumber - b.progNumber;
			case NAME:		return a.name.compare(b.name);
			case CAT:		return a.category.compare(b.category);
			default:		return 0;
		}
	}

	std::string PatchBrowser::getCellText(const jucePluginEditorLib::Patch& _patch, int _columnId)
	{
		switch (_columnId)
		{
		case INDEX:		return std::to_string(_patch.progNumber);
		case NAME:		return _patch.name;
		case CAT:		return static_cast<const Patch&>(_patch).category;
		default:		return "?";
		}
	}
}
