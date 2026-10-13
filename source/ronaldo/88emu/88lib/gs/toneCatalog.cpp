#include "88lib/gs/toneCatalog.h"

#include "88lib/rom/romloader.h"

#include <algorithm>

namespace emu88Lib::gs
{
	const Tone* ToneMap::find(const uint8_t _bankMsb, const uint8_t _program) const
	{
		const auto it = std::lower_bound(tones.begin(), tones.end(), std::pair{_program, _bankMsb},
			[](const Tone& _t, const std::pair<uint8_t, uint8_t>& _key)
			{
				return std::pair{_t.program, _t.bankMsb} < _key;
			});
		return it != tones.end() && it->program == _program && it->bankMsb == _bankMsb ? &*it : nullptr;
	}

	const DrumSet* ToneMap::findDrumSet(const uint8_t _program) const
	{
		for(const auto& set : drumSets)
		{
			if(set.program == _program)
				return &set;
		}
		return nullptr;
	}

	std::vector<const Tone*> ToneMap::variations(const uint8_t _program) const
	{
		std::vector<const Tone*> result;
		for(const auto& tone : tones)
		{
			if(tone.program == _program)
				result.push_back(&tone);
		}
		return result;
	}

	const ToneMap* ToneCatalog::findMap(const uint8_t _number) const
	{
		for(const auto& map : maps)
		{
			if(map.number == _number)
				return &map;
		}
		return nullptr;
	}

	ToneCatalog::ToneResult ToneCatalog::selectTone(const uint8_t _map, const uint8_t _bankMsb, const uint8_t _program,
		const bool _keep) const
	{
		const auto* map = findMap(_map);
		// A hidden map: the tone is whatever the board has there
		if(!map && std::find(hiddenMaps.begin(), hiddenMaps.end(), _map) != hiddenMaps.end())
			return {Selection::Tone, nullptr};
		if(map && map->userTones && (_bankMsb == 64 || _bankMsb == 65))
			return {Selection::User, nullptr};
		if(const auto* tone = map ? map->find(_bankMsb, _program) : nullptr)
			return {Selection::Tone, tone};
		// A map number the board does not have selects nothing, wherever it is from
		if(!map)
			return {missingTone == MissingTone::KeepIfFlagged && _keep ? Selection::Ignored : Selection::Silent, nullptr};

		switch(missingTone)
		{
		case MissingTone::SubCapital:
			// The SC-55 1.21's program change, 04:09C8 and on
			if(_bankMsb >= 0x40 || _program >= 0x78)
				return {};
			if(const auto* tone = map->find(_bankMsb & 0xf8, _program))
				return {Selection::Tone, tone};
			if(const auto* tone = map->find(0, _program))
				return {Selection::Tone, tone};
			return {};
		case MissingTone::KeepIfFlagged:
			return {_keep ? Selection::Ignored : Selection::Silent, nullptr};
		case MissingTone::KeepIfFlaggedBelow:
			return {_keep && _bankMsb < 0x40 && _program < 0x78 ? Selection::Ignored : Selection::Silent, nullptr};
		case MissingTone::Silent:
		default:
			return {};
		}
	}

	ToneCatalog::DrumSetResult ToneCatalog::selectDrumSet(const uint8_t _map, const uint8_t _program) const
	{
		const auto* map = findMap(_map);
		if(!map && std::find(hiddenMaps.begin(), hiddenMaps.end(), _map) != hiddenMaps.end())
			return {Selection::Tone, nullptr};
		if(!map)
			return {missingDrumSet == MissingDrumSet::Ignored ? Selection::Ignored : Selection::Silent, nullptr};
		if(map->userTones && (_program == 64 || _program == 65))
			return {Selection::User, nullptr};
		if(const auto* set = map->findDrumSet(_program))
			return {Selection::Tone, set};

		switch(missingDrumSet)
		{
		case MissingDrumSet::SubCapital:
			// 04:096E and on in the SC-55 1.21
			if(_program >= drumLimit)
				return {};
			if(const auto* set = map->findDrumSet(_program & 0xf8))
				return {Selection::Tone, set};
			if(const auto* set = map->findDrumSet(0))
				return {Selection::Tone, set};
			return {};
		case MissingDrumSet::Ignored:
			return {Selection::Ignored, nullptr};
		case MissingDrumSet::Silent:
		default:
			return {};
		}
	}

	std::shared_ptr<const ToneCatalog> loadToneCatalog(const DeviceModel _model, std::string& _error)
	{
		const auto inventory = RomLoader::scan();
		const auto device = RomLoader::toRomDevice(_model);
		auto catalog = std::make_shared<ToneCatalog>();

		const auto read = [&](std::vector<uint8_t>& _data, const RomSlot _slot)
		{
			if(inventory.read(_data, device, _slot))
				return true;
			_error = "The device's ROMs could not be read.";
			return false;
		};

		switch(_model)
		{
		case DeviceModel::Sc88:
		case DeviceModel::Sc88VL:
		case DeviceModel::Sc88Pro:
		case DeviceModel::Xpgs:
		case DeviceModel::VeGsPro:
			{
				std::vector<uint8_t> control;
				if(!read(control, RomSlot::Control) || !extractSc88Catalog(control, _model, *catalog, _error))
					return {};
				return catalog;
			}
		case DeviceModel::Sc8850:
		case DeviceModel::Sc8820:
			{
				// Where the boards map them: sc8850.cpp puts the internal ROM at 0, the program flash at 0x200000
				// and the data flash at 0xD00000; sc8820.cpp its program and tone ROM at 0xD00000.
				std::vector<RomRegion> regions(_model == DeviceModel::Sc8850 ? 3 : 2);
				if(!read(regions[0].data, RomSlot::Internal) || !read(regions[1].data, RomSlot::Program))
					return {};
				regions[0].code = true;
				regions[1].code = true;
				if(_model == DeviceModel::Sc8850)
				{
					regions[1].base = 0x200000;
					regions[2].base = 0xd00000;
					if(!read(regions[2].data, RomSlot::Data))
						return {};
				}
				else
				{
					regions[1].base = 0xd00000;
				}
				if(!extractSc8850Catalog(regions, _model, *catalog, _error))
					return {};
				return catalog;
			}
		default:
			if(isSc55Model(_model))
			{
				std::vector<uint8_t> program;
				if(!read(program, RomSlot::Program) || !extractSc55Catalog(program, _model, *catalog, _error))
					return {};
				return catalog;
			}
			_error = "The part editor does not know this device's tone lists.";
			return {};
		}
	}
}
