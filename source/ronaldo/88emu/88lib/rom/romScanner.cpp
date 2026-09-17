#include "88lib/rom/romloader.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>

#include "baseLib/filesystem.h"
#include "common/romDescramble.h"

namespace emu88Lib
{
    namespace
    {
        void swapWords(std::vector<uint8_t>& _data)
        {
            for (size_t i = 0; i + 1 < _data.size(); i += 2)
                std::swap(_data[i], _data[i + 1]);
        }

        const std::set<size_t>& registrySizes()
        {
            static const std::set<size_t> sizes = []
            {
                std::set<size_t> result;
                for (const auto& entry : g_romRegistry)
                    result.insert(entry.size);
                return result;
            }();
            return sizes;
        }

        bool anyWordSwappedAt(const size_t _size)
        {
            for (const auto& entry : g_romRegistry)
            {
                if (entry.size == _size && entry.wordSwapped)
                    return true;
            }
            return false;
        }

        // Only sizes where a revision was catalogued from a published digest need a
        // second pass over the data.
        bool anySha1At(const size_t _size)
        {
            for (const auto& entry : g_romRegistry)
            {
                if (entry.size == _size && !entry.hash.isValid() && entry.sha1.isValid())
                    return true;
            }
            return false;
        }

        // Reuse digests until the file size or modification time changes.
        struct HashCacheEntry
        {
            size_t size = 0;
            uint64_t modificationTime = 0;
            baseLib::MD5 raw;
            baseLib::MD5 swapped;
            bool hasSwapped = false;
            baseLib::SHA1 sha1;
            bool hasSha1 = false;
            std::vector<FoundRom> proControlCopies;
        };

        // A row is identified by its MD5 where the dump was hashed here, and by its
        // published SHA-1 where it was catalogued without one. Never by both, so a
        // revision cannot be recognized under two identities.
        bool matchesRaw(const RomRegistryEntry& _entry, const HashCacheEntry& _hashes)
        {
            if (_entry.hash.isValid())
                return _entry.hash == _hashes.raw;
            return _entry.sha1.isValid() && _hashes.hasSha1 && _entry.sha1 == _hashes.sha1;
        }

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
                           const bool _wantSwapped, const bool _wantSha1)
        {
            const auto modificationTime = baseLib::filesystem::getFileModificationTime(_file);

            {
                const std::lock_guard lock(hashCacheMutex());
                const auto it = hashCache().find(_file);
                if (it != hashCache().end() && it->second.size == _size &&
                    it->second.modificationTime == modificationTime && (!_wantSwapped || it->second.hasSwapped) &&
                    (!_wantSha1 || it->second.hasSha1))
                {
                    _result = it->second;
                    return true;
                }
            }

            std::vector<uint8_t> data;
            if (!baseLib::filesystem::readFile(data, _file) || data.size() != _size)
                return false;

            _result.size = _size;
            _result.modificationTime = modificationTime;
            _result.raw = baseLib::MD5(data);
            _result.proControlCopies.clear();
            // Oversized EPROM reads can contain damaged mirrors followed by
            // intact copies. Examine every 1 MiB bank in 2/4 MiB dumps; never
            // assume the first copy (or a filename-specific offset) is correct.
            if (_size == 2 * Sc88ProRomSet::FirmwareSize || _size == 4 * Sc88ProRomSet::FirmwareSize)
            {
                std::set<const RomRegistryEntry*> found;
                for (size_t offset = 0; offset < _size; offset += Sc88ProRomSet::FirmwareSize)
                {
                    std::vector<uint8_t> copy(data.begin() + offset,
                                              data.begin() + offset + Sc88ProRomSet::FirmwareSize);
                    for (const bool swapped : {false, true})
                    {
                        if (swapped)
                            swapWords(copy);
                        const auto* entry = findRegistryEntry(copy.size(), baseLib::MD5(copy), swapped);
                        if (entry && (usedBy(*entry, RomDevice::Sc88Pro) || usedBy(*entry, RomDevice::VeGsPro)) &&
                            entry->slot == RomSlot::Control && found.insert(entry).second)
                        {
                            FoundRom rom;
                            rom.entry = entry;
                            rom.path = _file;
                            rom.needsWordSwap = swapped;
                            rom.fileOffset = offset;
                            rom.embedded = true;
                            _result.proControlCopies.push_back(std::move(rom));
                        }
                    }
                }
            }
            // Before any swapping: a published digest always describes the file as it
            // is on disk.
            _result.hasSha1 = _wantSha1;
            if (_wantSha1)
                _result.sha1 = baseLib::SHA1(data);

            _result.hasSwapped = _wantSwapped;
            if (_wantSwapped)
            {
                swapWords(data);
                _result.swapped = baseLib::MD5(data);
            }

            const std::lock_guard lock(hashCacheMutex());
            hashCache()[_file] = _result;
            return true;
        }

        // Reuse the inventory across component loads; rescan() discovers new files.
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

        const RomRegistryEntry* matchNamedHash(const RomFileSpec& _spec, const HashCacheEntry& _hashes,
                                               bool& _knownHash, bool& _needsWordSwap)
        {
            _knownHash = false;
            _needsWordSwap = false;
            for (const auto& entry : g_romRegistry)
            {
                if (!usedBy(entry, _spec.device) || entry.slot != _spec.slot || entry.index != _spec.index ||
                    entry.size != _spec.size)
                    continue;
                _knownHash = true;
                if (matchesRaw(entry, _hashes))
                    return &entry;
                if (_hashes.hasSwapped && entry.wordSwapped && entry.hash.isValid() && entry.hash == _hashes.swapped)
                {
                    _needsWordSwap = true;
                    return &entry;
                }
            }
            return nullptr;
        }


    } // namespace
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
        if (size < Sc8820WaveRomSet::Cs1Size)
            return {};
        {
            const std::lock_guard lock(mutex);
            const auto it = cache.find(_path);
            if (it != cache.end() && it->second.size == size && it->second.modificationTime == modificationTime)
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
        for (const auto& entry : g_romRegistry)
            if (usedBy(entry, RomDevice::Sc8820) && entry.slot == RomSlot::Wave && entry.index < entries.size())
                entries[entry.index] = &entry;

        constexpr size_t blockSize = 0x100000;
        constexpr size_t headerSize = 40;
        std::vector<uint8_t> block, wave;
        std::vector<FoundRom> result;
        for (size_t offset = 0; offset < size && result.size() < entries.size();
             offset += std::min(blockSize, size - offset))
        {
            // Overlap catches headers spanning scan-block boundaries, while
            // memory usage stays bounded even for large universal libraries.
            if (!baseLib::filesystem::readFileRegion(block, _path, offset,
                                                     std::min(blockSize + headerSize - 1, size - offset)))
                return {};
            auto cursor = block.cbegin();
            while (cursor != block.cend())
            {
                const auto hit = std::search(cursor, block.cend(), std::begin(signature), std::end(signature));
                const auto position = static_cast<size_t>(hit - block.cbegin());
                if (position >= blockSize || static_cast<size_t>(block.cend() - hit) < headerSize)
                    break;
                cursor = hit + 1;
                for (size_t index = 0; index < entries.size(); ++index)
                {
                    const auto* entry = entries[index];
                    if (!entry || !std::equal(std::begin(names[index]), std::end(names[index]), hit + 0x20) ||
                        entry->size > size - offset - position)
                        continue;
                    if (baseLib::filesystem::readFileRegion(wave, _path, offset + position, entry->size) &&
                        baseLib::MD5(wave) == entry->hash)
                    {
                        FoundRom rom;
                        rom.entry = entry;
                        rom.path = _path;
                        rom.fileOffset = offset + position;
                        rom.embedded = true;
                        result.push_back(std::move(rom));
                        entries[index] = nullptr;
                    }
                }
            }
        }
        if (baseLib::filesystem::getFileSize(_path) != size ||
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
        if (sizes.empty())
            return inventory;

        // Raw images retain the size filter below. Containers can be larger
        // than any ROM, and only DLL/dylib files enter the embedded scan.
        auto files = synthLib::RomLoader::findFilesRecursive({}, *sizes.begin(), 0);
        std::sort(files.begin(), files.end(), [](const auto& _a, const auto& _b) { return _a.path < _b.path; });

        std::set<const RomRegistryEntry*> taken;
        std::vector<FoundRom> proControlCopies;

        // Named, correctly sized images are authoritative for their model. This
        // pass deliberately runs before content addressing so a custom build can
        // replace a stock image without being shadowed by another known dump.
        for (const auto& spec : g_romFileSpecs)
        {
            const auto expected = baseLib::filesystem::lowercase(spec.filename);
            for (const auto& [path, size] : files)
            {
                if (size != spec.size ||
                    baseLib::filesystem::lowercase(baseLib::filesystem::getFilenameWithoutPath(path)) != expected)
                    continue;

                HashCacheEntry hashes;
                if (!hashCandidate(hashes, path, size, spec.normalizeH8Words || anyWordSwappedAt(size),
                                   anySha1At(size)))
                    continue;
                bool knownHash = false;
                bool needsWordSwap = false;
                const auto* entry = matchNamedHash(spec, hashes, knownHash, needsWordSwap);
                FoundRom rom;
                rom.entry = entry;
                rom.namedSpec = &spec;
                rom.path = path;
                rom.needsWordSwap = needsWordSwap;
                rom.hashMismatch = knownHash && !entry;
                rom.actualHash = hashes.raw;
                inventory.add(std::move(rom));
                if (entry)
                    taken.insert(entry);
                break;
            }
        }

        for (const auto& [path, size] : files)
        {
            if (!sizes.count(size))
                continue;

            HashCacheEntry hashes;
            if (!hashCandidate(hashes, path, size, anyWordSwappedAt(size), anySha1At(size)))
                continue;
            proControlCopies.insert(proControlCopies.end(), hashes.proControlCopies.begin(),
                                    hashes.proControlCopies.end());

            // One image may serve different slots on different devices.
            for (const auto& entry : g_romRegistry)
            {
                if (entry.size != size)
                    continue;
                const bool rawMatch = matchesRaw(entry, hashes);
                const bool swappedMatch = hashes.hasSwapped && entry.wordSwapped && entry.hash.isValid() &&
                    entry.hash == hashes.swapped;
                if ((!rawMatch && !swappedMatch) || !taken.insert(&entry).second)
                    continue;

                FoundRom rom;
                rom.entry = &entry;
                rom.path = path;
                rom.needsWordSwap = !rawMatch;
                rom.actualHash = hashes.raw;
                inventory.add(std::move(rom));
            }
        }
        for (auto& rom : proControlCopies)
            if (taken.insert(rom.entry).second)
                inventory.add(std::move(rom));

        // Prefer standalone dumps when both sources provide the same image.
        for (const auto& [path, size] : files)
        {
            if (size < Sc8820WaveRomSet::Cs1Size ||
                (!baseLib::filesystem::hasExtension(path, ".dll") &&
                 !baseLib::filesystem::hasExtension(path, ".dylib")))
                continue;
            for (auto rom : findEmbeddedWaveRoms(path))
                if (taken.insert(rom.entry).second)
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
            if (inventoryIsCurrent())
                return cachedInventory();
        }
        return rescan();
    }


} // namespace emu88Lib
