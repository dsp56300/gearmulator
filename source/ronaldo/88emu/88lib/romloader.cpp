#include "romloader.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <set>

#include "baseLib/filesystem.h"

namespace emu88Lib
{
	namespace
	{
		bool readFileRegion(std::vector<uint8_t>& _data, const std::string& _path,
		                    const size_t _offset, const size_t _size)
		{
			const std::unique_ptr<FILE, decltype(&std::fclose)> file(
				baseLib::filesystem::openFile(_path, "rb"), &std::fclose);
			_data.clear();
			if(!file)
				return false;
#ifdef _WIN32
			if(_fseeki64(file.get(), static_cast<int64_t>(_offset), SEEK_SET) != 0)
#else
			if(fseeko(file.get(), static_cast<off_t>(_offset), SEEK_SET) != 0)
#endif
				return false;
			_data.resize(_size);
			if(std::fread(_data.data(), 1, _size, file.get()) == _size)
				return true;
			_data.clear();
			return false;
		}

		void swapWords(std::vector<uint8_t>& _data)
		{
			for(size_t i = 0; i + 1 < _data.size(); i += 2)
				std::swap(_data[i], _data[i + 1]);
		}

		// Inverse of WaveRom::unscramble. Pro ROM sets contain physical chip data,
		// whereas the registered SC-8820/8850 dumps are already decoded. Every
		// slice here starts on a 1 MiB boundary and contains complete blocks.
		std::vector<uint8_t> scrambleProWave(const uint8_t* _decoded, const size_t _size)
		{
			std::vector<uint8_t> raw(_size);
			for(size_t i = 0; i < _size; ++i)
			{
				size_t address = i & ~static_cast<size_t>(0xfffff);
				for(int j = 0; j < 20; ++j)
					address |= ((i >> j) & 1) << WaveRom::g_addressPermutation[j];

				uint8_t value = 0;
				for(int j = 0; j < 8; ++j)
					value |= ((_decoded[i] >> j) & 1) << WaveRom::g_dataPermutation[j];
				raw[address] = value;
			}
			return raw;
		}

		// The only sizes worth opening. Everything else on the search paths is
		// discarded on its stat, which is what keeps a recursive sweep of an
		// unsorted ROM collection cheap.
		const std::set<size_t>& registrySizes()
		{
			static const std::set<size_t> sizes = []
			{
				std::set<size_t> result;
				for(const auto& entry : g_romRegistry)
					result.insert(entry.size);
				return result;
			}();
			return sizes;
		}

		bool anyWordSwappedAt(const size_t _size)
		{
			for(const auto& entry : g_romRegistry)
			{
				if(entry.size == _size && entry.wordSwapped)
					return true;
			}
			return false;
		}

		// Hashing every candidate is the expensive part of a scan, and a scan
		// happens whenever a device is created or the model is switched. The
		// digests are therefore remembered for the lifetime of the process,
		// keyed by path *and* size and modification time so that replacing a
		// dump in place is still noticed.
		struct HashCacheEntry
		{
			size_t size = 0;
			uint64_t modificationTime = 0;
			baseLib::MD5 raw;
			baseLib::MD5 swapped;
			bool hasSwapped = false;
			std::vector<FoundRom> proControlCopies;
		};

		std::mutex& hashCacheMutex()
		{
			static std::mutex mutex;
			return mutex;
		}

		std::map<std::string, HashCacheEntry>& hashCache()
		{
			static std::map<std::string, HashCacheEntry> cache;
			return cache;
		}

		// Digests for one candidate, from the cache where possible. Returns
		// false if the file could not be read at its expected size.
		bool hashCandidate(HashCacheEntry& _result, const std::string& _file, const size_t _size,
		                   const bool _wantSwapped)
		{
			const auto modificationTime = baseLib::filesystem::getFileModificationTime(_file);

			{
				const std::lock_guard lock(hashCacheMutex());
				const auto it = hashCache().find(_file);
				if(it != hashCache().end() && it->second.size == _size &&
				   it->second.modificationTime == modificationTime &&
				   (!_wantSwapped || it->second.hasSwapped))
				{
					_result = it->second;
					return true;
				}
			}

			std::vector<uint8_t> data;
			if(!baseLib::filesystem::readFile(data, _file) || data.size() != _size)
				return false;

			_result.size = _size;
			_result.modificationTime = modificationTime;
			_result.raw = baseLib::MD5(data);
			_result.proControlCopies.clear();
			// Oversized EPROM reads can contain damaged mirrors followed by
			// intact copies. Examine every 1 MiB bank in 2/4 MiB dumps; never
			// assume the first copy (or a filename-specific offset) is correct.
			if(_size == 2 * Sc88ProRomSet::FirmwareSize || _size == 4 * Sc88ProRomSet::FirmwareSize)
			{
				std::set<const RomRegistryEntry*> found;
				for(size_t offset = 0; offset < _size; offset += Sc88ProRomSet::FirmwareSize)
				{
					std::vector<uint8_t> copy(data.begin() + offset,
					                          data.begin() + offset + Sc88ProRomSet::FirmwareSize);
					for(const bool swapped : {false, true})
					{
						if(swapped)
							swapWords(copy);
						const auto* entry = findRegistryEntry(copy.size(), baseLib::MD5(copy), swapped);
						if(entry && usedBy(*entry, RomDevice::Sc88Pro) && entry->slot == RomSlot::Control &&
						   found.insert(entry).second)
							_result.proControlCopies.push_back({entry, _file, swapped, offset, true});
					}
				}
			}
			_result.hasSwapped = _wantSwapped;
			if(_wantSwapped)
			{
				swapWords(data);
				_result.swapped = baseLib::MD5(data);
			}

			const std::lock_guard lock(hashCacheMutex());
			hashCache()[_file] = _result;
			return true;
		}

		// A board's ROM set is looked up several times in a row when a device is
		// created - once per component - and the search paths do not change
		// underneath a running session unless the user puts a new dump there.
		// So the sweep result is kept, and rescan() is what picks new files up.
		std::mutex& inventoryMutex()
		{
			static std::mutex mutex;
			return mutex;
		}

		RomInventory& cachedInventory()
		{
			static RomInventory inventory;
			return inventory;
		}

		bool& inventoryIsCurrent()
		{
			static bool current = false;
			return current;
		}

		// The distinct slots a board reads, in registry order. Two rows for the
		// same slot and index are two revisions of one image, not two images.
		std::vector<std::pair<RomSlot, uint8_t>> requiredSlots(const RomDevice _device)
		{
			std::vector<std::pair<RomSlot, uint8_t>> result;
			for(const auto& entry : g_romRegistry)
			{
				if(!usedBy(entry, _device))
					continue;
				const std::pair<RomSlot, uint8_t> slot{entry.slot, entry.index};
				if(std::find(result.begin(), result.end(), slot) == result.end())
					result.push_back(slot);
			}
			return result;
		}

		Rom readRom(const RomInventory& _inventory, const RomDevice _device, const RomSlot _slot)
		{
			const auto* found = _inventory.find(_device, _slot);
			std::vector<uint8_t> data;
			if(!found || !_inventory.read(data, _device, _slot))
				return {};
			return {std::move(data), found->entry};
		}
	}

	// =====================================================================
	// RomInventory
	// =====================================================================

	// Several revisions of one image can be present at once - three control
	// ROMs run on the SC-88Pro board, for instance. Registry order is
	// preference order, so the result must not depend on which file the
	// directory sweep happened to reach first.
	const FoundRom* RomInventory::find(const RomDevice _device, const RomSlot _slot, const uint8_t _index) const
	{
		const FoundRom* result = nullptr;

		for(const auto& rom : m_roms)
		{
			if(rom.entry->slot != _slot || rom.entry->index != _index || !usedBy(*rom.entry, _device))
				continue;
			if(!result || rom.entry < result->entry)
				result = &rom;
		}
		return result;
	}

	const FoundRom* RomInventory::findSc88ProWaveSource(const uint8_t _index, size_t& _offset) const
	{
		if(_index > 2)
			return nullptr;

		constexpr size_t offsets[] = {0, Sc88ProRomSet::WaveASize,
		                             Sc88ProRomSet::WaveASize + Sc88ProRomSet::WaveBSize};
		_offset = offsets[_index];
		if(const auto* source = find(RomDevice::Sc8850, RomSlot::Wave))
			return source;

		// CS0 contains A+B; the first 4 MiB of CS1 contains C. No donor
		// firmware is needed, and native Pro chips may fill the other slots.
		if(_index == 2)
			_offset = 0;
		return find(RomDevice::Sc8820, RomSlot::Wave, _index == 2 ? 1 : 0);
	}

	bool RomInventory::has(const RomDevice _device, const RomSlot _slot, const uint8_t _index) const
	{
		if(find(_device, _slot, _index))
			return true;
		size_t offset = 0;
		return _device == RomDevice::Sc88Pro && _slot == RomSlot::Wave &&
		       findSc88ProWaveSource(_index, offset) != nullptr;
	}

	bool RomInventory::read(std::vector<uint8_t>& _data, const RomDevice _device, const RomSlot _slot,
	                        const uint8_t _index) const
	{
		const auto* found = find(_device, _slot, _index);
		size_t sourceOffset = 0;
		const bool deriveProWave = !found && _device == RomDevice::Sc88Pro && _slot == RomSlot::Wave;
		if(deriveProWave)
			found = findSc88ProWaveSource(_index, sourceOffset);
		if(!found)
			return false;

		const bool read = found->embedded
			? readFileRegion(_data, found->path, found->fileOffset, found->entry->size)
			: baseLib::filesystem::readFile(_data, found->path);
		if(!read || _data.size() != found->entry->size)
		{
			_data.clear();
			return false;
		}

		if(found->needsWordSwap)
			swapWords(_data);
		if(found->embedded && baseLib::MD5(_data) != found->entry->hash)
		{
			_data.clear();
			return false;
		}
		if(deriveProWave)
		{
			const size_t size = _index == 2 ? Sc88ProRomSet::WaveCSize : Sc88ProRomSet::WaveASize;
			if(sourceOffset + size > _data.size())
			{
				_data.clear();
				return false;
			}
			_data = scrambleProWave(_data.data() + sourceOffset, size);
			const auto* entry = findRegistryEntry(size, baseLib::MD5(_data));
			if(!entry || !usedBy(*entry, RomDevice::Sc88Pro) || entry->slot != RomSlot::Wave ||
			   entry->index != _index)
			{
				_data.clear();
				return false;
			}
		}
		return true;
	}

	bool RomInventory::isComplete(const RomDevice _device) const
	{
		const auto slots = requiredSlots(_device);
		if(slots.empty())
			return false;

		for(const auto& [slot, index] : slots)
		{
			if(!has(_device, slot, index))
				return false;
		}
		return true;
	}

	std::vector<const RomRegistryEntry*> RomInventory::missing(const RomDevice _device) const
	{
		std::vector<const RomRegistryEntry*> result;
		std::vector<std::pair<RomSlot, uint8_t>> reported;

		for(const auto& entry : g_romRegistry)
		{
			if(!usedBy(entry, _device) || has(_device, entry.slot, entry.index))
				continue;

			const std::pair<RomSlot, uint8_t> slot{entry.slot, entry.index};
			if(std::find(reported.begin(), reported.end(), slot) != reported.end())
				continue;

			reported.push_back(slot);
			result.push_back(&entry);
		}
		return result;
	}

	// =====================================================================
	// RomLoader
	// =====================================================================

	std::vector<FoundRom> RomLoader::findEmbeddedWaveRoms(const std::string& _path)
	{
		struct CacheEntry
		{
			size_t size;
			uint64_t modificationTime;
			std::vector<FoundRom> roms;
		};
		static std::mutex mutex;
		static std::map<std::string, CacheEntry> cache;
		const auto size = baseLib::filesystem::getFileSize(_path);
		const auto modificationTime = baseLib::filesystem::getFileModificationTime(_path);
		if(size < Sc8820WaveRomSet::Cs1Size)
			return {};
		{
			const std::lock_guard lock(mutex);
			const auto it = cache.find(_path);
			if(it != cache.end() && it->second.size == size && it->second.modificationTime == modificationTime)
				return it->second.roms;
		}

		// Decoded service-header signature and build names at header +0x20.
		// Headers repeat within each chip, so they only nominate candidates;
		// the complete region must match a registered wave ROM. Search each
		// chip independently: libraries may store CS1 before CS0, add padding,
		// or contain multiple architecture copies of both.
		constexpr uint8_t signature[] = {0xa4, 0xeb, 0xa5, 0x2b, 0xe9, 0x29};
		constexpr uint8_t names[2][8] = {{'v', 'e', 'r', '2', '0', '0', 0, 0},
		                                {'r', 'o', 'm', '_', 'm', 'a', 'k', 'e'}};
		std::array<const RomRegistryEntry*, 2> entries{};
		for(const auto& entry : g_romRegistry)
			if(usedBy(entry, RomDevice::Sc8820) && entry.slot == RomSlot::Wave && entry.index < entries.size())
				entries[entry.index] = &entry;

		constexpr size_t blockSize = 0x100000;
		constexpr size_t headerSize = 40;
		std::vector<uint8_t> block, wave;
		std::vector<FoundRom> result;
		for(size_t offset = 0; offset < size && result.size() < entries.size();
		    offset += std::min(blockSize, size - offset))
		{
			// Overlap catches headers spanning scan-block boundaries, while
			// memory usage stays bounded even for large universal libraries.
			if(!readFileRegion(block, _path, offset, std::min(blockSize + headerSize - 1, size - offset)))
				return {};
			auto cursor = block.cbegin();
			while(cursor != block.cend())
			{
				const auto hit = std::search(cursor, block.cend(), std::begin(signature), std::end(signature));
				const auto position = static_cast<size_t>(hit - block.cbegin());
				if(position >= blockSize || static_cast<size_t>(block.cend() - hit) < headerSize)
					break;
				cursor = hit + 1;
				for(size_t index = 0; index < entries.size(); ++index)
				{
					const auto* entry = entries[index];
					if(!entry || !std::equal(std::begin(names[index]), std::end(names[index]), hit + 0x20) ||
					   entry->size > size - offset - position)
						continue;
					if(readFileRegion(wave, _path, offset + position, entry->size) && baseLib::MD5(wave) == entry->hash)
					{
						result.push_back({entry, _path, false, offset + position, true});
						entries[index] = nullptr;
					}
				}
			}
		}
		if(baseLib::filesystem::getFileSize(_path) != size ||
		   baseLib::filesystem::getFileModificationTime(_path) != modificationTime)
			return {};
		const std::lock_guard lock(mutex);
		cache[_path] = {size, modificationTime, result};
		return result;
	}

	RomInventory RomLoader::rescan()
	{
		RomInventory inventory;

		const auto& sizes = registrySizes();
		if(sizes.empty())
			return inventory;

		// Raw images retain the size filter below. Containers can be larger
		// than any ROM, and only DLL/dylib files enter the embedded scan.
		const auto files = synthLib::RomLoader::findFilesRecursive({}, *sizes.begin(), 0);

		std::set<const RomRegistryEntry*> taken;
		std::vector<FoundRom> proControlCopies;

		for(const auto& [path, size] : files)
		{
			if(!sizes.count(size))
				continue;

			HashCacheEntry hashes;
			if(!hashCandidate(hashes, path, size, anyWordSwappedAt(size)))
				continue;
			proControlCopies.insert(proControlCopies.end(), hashes.proControlCopies.begin(), hashes.proControlCopies.end());

			bool needsWordSwap = false;
			const auto* entry = findRegistryEntry(size, hashes.raw);
			if(!entry && hashes.hasSwapped)
			{
				entry = findRegistryEntry(size, hashes.swapped, true);
				needsWordSwap = entry != nullptr;
			}

			// The same dump can legitimately appear more than once on the
			// search paths; the first one found wins.
			if(!entry || !taken.insert(entry).second)
				continue;

			inventory.add({entry, path, needsWordSwap});
		}
		for(auto& rom : proControlCopies)
			if(taken.insert(rom.entry).second)
				inventory.add(std::move(rom));

		// Prefer standalone dumps when both sources provide the same image.
		for(const auto& [path, size] : files)
		{
			if(size < Sc8820WaveRomSet::Cs1Size ||
			   (!baseLib::filesystem::hasExtension(path, ".dll") &&
			    !baseLib::filesystem::hasExtension(path, ".dylib")))
				continue;
			for(auto rom : findEmbeddedWaveRoms(path))
				if(taken.insert(rom.entry).second)
					inventory.add(std::move(rom));
		}

		{
			const std::lock_guard lock(inventoryMutex());
			cachedInventory() = inventory;
			inventoryIsCurrent() = true;
		}
		return inventory;
	}

	RomInventory RomLoader::scan()
	{
		{
			const std::lock_guard lock(inventoryMutex());
			if(inventoryIsCurrent())
				return cachedInventory();
		}
		return rescan();
	}

	bool RomLoader::isDeviceAvailable(const RomDevice _device)
	{
		return scan().isComplete(_device);
	}

	bool RomLoader::isDeviceAvailable(const DeviceModel _model)
	{
		return isDeviceAvailable(toRomDevice(_model));
	}

	RomDevice RomLoader::toRomDevice(const DeviceModel _model)
	{
		switch(_model)
		{
		case DeviceModel::Sc88:    return RomDevice::Sc88;
		case DeviceModel::Sc88VL:  return RomDevice::Sc88VL;
		case DeviceModel::Sc88Pro: return RomDevice::Sc88Pro;
		case DeviceModel::Sc8850:  return RomDevice::Sc8850;
		case DeviceModel::Sc55Mk2: return RomDevice::Sc55Mk2;
		}
		return RomDevice::Sc88;
	}

	Rom RomLoader::findROM(const Model _model)
	{
		const auto device = _model == Model::Sc88VL ? RomDevice::Sc88VL : RomDevice::Sc88;
		return readRom(scan(), device, RomSlot::Control);
	}

	Sc88ProRomSet RomLoader::findSc88ProRomSet()
	{
		const auto inventory = scan();
		if(!inventory.isComplete(RomDevice::Sc88Pro))
			return {};

		Sc88ProRomSet result;
		if(!inventory.read(result.firmware, RomDevice::Sc88Pro, RomSlot::Control) ||
		   !inventory.read(result.waveA, RomDevice::Sc88Pro, RomSlot::Wave, 0) ||
		   !inventory.read(result.waveB, RomDevice::Sc88Pro, RomSlot::Wave, 1) ||
		   !inventory.read(result.waveC, RomDevice::Sc88Pro, RomSlot::Wave, 2))
			return {};

		if(const auto* found = inventory.find(RomDevice::Sc88Pro, RomSlot::Control))
			result.firmwareName = describe(*found->entry);

		return result.isValid() ? result : Sc88ProRomSet{};
	}

	Sc8850RomSet RomLoader::findSc8850RomSet()
	{
		const auto inventory = scan();

		Sc8850RomSet result;
		if(!inventory.read(result.cpu, RomDevice::Sc8850, RomSlot::Internal) ||
		   !inventory.read(result.program, RomDevice::Sc8850, RomSlot::Program) ||
		   !inventory.read(result.data, RomDevice::Sc8850, RomSlot::Data))
			return {};

		return result.isValid() ? result : Sc8850RomSet{};
	}

	Sc8850WaveRomSet RomLoader::findSc8850WaveRomSet()
	{
		std::vector<uint8_t> data;
		if(!scan().read(data, RomDevice::Sc8850, RomSlot::Wave) || data.size() != Sc8850WaveRomSet::Size)
			return {};

		Sc8850WaveRomSet result;
		const auto second = data.begin() + Sc8850WaveRomSet::ChipSize;
		result.romA.assign(data.begin(), second);
		result.romB.assign(second, data.end());
		return result;
	}

	Sc55RomSet RomLoader::findSc55RomSet()
	{
		const auto inventory = scan();

		Sc55RomSet result;
		if(!inventory.read(result.internalRom, RomDevice::Sc55Mk2, RomSlot::Internal) ||
		   !inventory.read(result.programRom, RomDevice::Sc55Mk2, RomSlot::Program) ||
		   !inventory.read(result.waveRom[0], RomDevice::Sc55Mk2, RomSlot::Wave, 0) ||
		   !inventory.read(result.waveRom[1], RomDevice::Sc55Mk2, RomSlot::Wave, 1))
			return {};

		return result.isValid() ? result : Sc55RomSet{};
	}

	WaveRom RomLoader::findWaveRom()
	{
		const auto inventory = scan();

		std::array<std::vector<uint8_t>, WaveRom::ChipCount> chips;
		for(uint8_t bank = 0; bank < WaveRom::ChipCount; ++bank)
			inventory.read(chips[bank], RomDevice::Sc88, RomSlot::Wave, bank);

		return WaveRom(chips);
	}
}
