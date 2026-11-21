#include "88lib/rom/romloader.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>

#include "88lib/rom/romSignatures.h"

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
                for (const auto& mirrored : g_romMirroredSlots)
                    result.insert(mirrored.dumpSize);
                return result;
            }();
            return sizes;
        }

        // Whether _data is one block of _size repeated end to end, at least twice.
        bool isRepeated(const std::vector<uint8_t>& _data, const size_t _size)
        {
            if (_data.size() <= _size || _data.size() % _size)
                return false;
            for (size_t offset = _size; offset < _data.size(); offset += _size)
            {
                if (!std::equal(_data.begin(), _data.begin() + _size, _data.begin() + offset))
                    return false;
            }
            return true;
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

        bool anySha256At(const size_t _size)
        {
            for (const auto& entry : g_romRegistry)
            {
                if (entry.size == _size && !entry.hash.isValid() && entry.sha256.isValid())
                    return true;
            }
            return false;
        }

        // ---- signatures: the first four bytes of an image, byte 0 in the top bits

        uint32_t headOf(const uint8_t* _data)
        {
            return static_cast<uint32_t>(_data[0]) << 24 | static_cast<uint32_t>(_data[1]) << 16 |
                static_cast<uint32_t>(_data[2]) << 8 | static_cast<uint32_t>(_data[3]);
        }

        // The head of the same bytes with the two bytes of each word swapped
        uint32_t swapHead(const uint32_t _head)
        {
            return (_head & 0xff00ff00u) >> 8 | (_head & 0x00ff00ffu) << 8;
        }

        bool readHead(uint32_t& _head, const std::string& _file, const size_t _offset)
        {
            std::vector<uint8_t> data;
            if (!baseLib::filesystem::readFileRegion(data, _file, _offset, 4) || data.size() != 4)
                return false;
            _head = headOf(data.data());
            return true;
        }

        // The signature of a registry row, if one is known
        const std::optional<uint32_t>& signatureOf(const RomRegistryEntry& _entry)
        {
            static const auto signatures = []
            {
                std::vector<std::optional<uint32_t>> result(std::size(g_romRegistry));
                for (size_t i = 0; i < result.size(); ++i)
                {
                    if (!g_romRegistry[i].hash.isValid())
                        continue;
                    for (const auto& signature : g_romSignatures)
                    {
                        if (signature.hash == g_romRegistry[i].hash)
                        {
                            result[i] = signature.head;
                            break;
                        }
                    }
                }
                return result;
            }();
            return signatures[static_cast<size_t>(&_entry - std::begin(g_romRegistry))];
        }

        // Whether an image whose first four bytes are _head can be the MD5 of _entry, as it is or,
        // with _swapped, once its words are swapped. A row without a signature can be anything.
        bool signatureAllows(const RomRegistryEntry& _entry, const uint32_t _head, const bool _swapped)
        {
            if (!_entry.hash.isValid() || (_swapped && !_entry.wordSwapped))
                return false;
            const auto& signature = signatureOf(_entry);
            return !signature || *signature == (_swapped ? swapHead(_head) : _head);
        }

        bool isProEpromRead(const size_t _size)
        {
            return _size == 2 * Sc88ProRomSet::FirmwareSize || _size == 4 * Sc88ProRomSet::FirmwareSize;
        }

        bool isProControl(const RomRegistryEntry& _entry)
        {
            return _entry.size == Sc88ProRomSet::FirmwareSize && _entry.slot == RomSlot::Control &&
                (usedBy(_entry, RomDevice::Sc88Pro) || usedBy(_entry, RomDevice::VeGsPro));
        }

        // Whether a bank of an oversized EPROM read starting with _head can be a Pro control ROM
        bool proControlAllows(const uint32_t _head, const bool _swapped)
        {
            for (const auto& entry : g_romRegistry)
            {
                if (isProControl(entry) && signatureAllows(entry, _head, _swapped))
                    return true;
            }
            return false;
        }

        bool isMirroredImage(const RomRegistryEntry& _entry, const RomMirroredSlot& _mirrored)
        {
            return usedBy(_entry, _mirrored.device) && _entry.slot == _mirrored.slot &&
                _entry.index == _mirrored.index && _entry.size == _mirrored.imageSize;
        }

        bool mirroredAllows(const RomMirroredSlot& _mirrored, const uint32_t _head)
        {
            for (const auto& entry : g_romRegistry)
            {
                if (isMirroredImage(entry, _mirrored) && signatureAllows(entry, _head, false))
                    return true;
            }
            return false;
        }

        // The digests a candidate needs
        struct HashPlan
        {
            bool raw = false;
            bool swapped = false;
            bool sha1 = false;
            bool sha256 = false;
            // A known image may lie inside the file: a Pro control ROM in a bank of an oversized
            // EPROM read, or the image a mirrored dump repeats. Looked for whenever the file is read.
            bool embedded = false;

            bool any() const { return raw || swapped || sha1 || sha256 || embedded; }
        };

        // Everything a file of _size can be identified by, for a file of a standardized name
        HashPlan fullPlan(const size_t _size, const bool _normalizeH8Words)
        {
            HashPlan plan;
            plan.raw = true;
            plan.swapped = _normalizeH8Words || anyWordSwappedAt(_size);
            plan.sha1 = anySha1At(_size);
            plan.sha256 = anySha256At(_size);
            plan.embedded = true;
            return plan;
        }

        // What a file of _size can be, judged by its first bytes before it is read whole: only
        // digests of rows whose signatures those bytes allow. Rows known by a published digest
        // have no signature, so the files of their size are always hashed for them. Nothing
        // planned means no row can match and the file is not read at all.
        HashPlan planHashes(const std::string& _file, const size_t _size)
        {
            HashPlan plan;
            uint32_t head = 0;
            if (!readHead(head, _file, 0))
                return plan;

            for (const auto& entry : g_romRegistry)
            {
                if (entry.size != _size)
                    continue;
                if (!entry.hash.isValid())
                {
                    plan.sha1 |= entry.sha1.isValid();
                    plan.sha256 |= entry.sha256.isValid();
                    continue;
                }
                plan.raw |= signatureAllows(entry, head, false);
                plan.swapped |= signatureAllows(entry, head, true);
            }
            // A word-swapped match reports the digest of the file as it is
            plan.raw |= plan.swapped;

            if (isProEpromRead(_size))
            {
                for (size_t offset = 0; offset < _size && !plan.embedded; offset += Sc88ProRomSet::FirmwareSize)
                {
                    uint32_t bankHead = 0;
                    plan.embedded = readHead(bankHead, _file, offset) &&
                        (proControlAllows(bankHead, false) || proControlAllows(bankHead, true));
                }
            }
            for (const auto& mirrored : g_romMirroredSlots)
                plan.embedded |= _size == mirrored.dumpSize && mirroredAllows(mirrored, head);

            return plan;
        }

        // Reuse digests until the file size or modification time changes.
        struct HashCacheEntry
        {
            size_t size = 0;
            uint64_t modificationTime = 0;
            baseLib::MD5 raw;
            bool hasRaw = false;
            baseLib::MD5 swapped;
            bool hasSwapped = false;
            baseLib::SHA1 sha1;
            bool hasSha1 = false;
            baseLib::SHA256 sha256;
            bool hasSha256 = false;
            // Known images inside the file: Pro control ROMs in an oversized EPROM read, and the
            // image a mirrored dump repeats.
            std::vector<FoundRom> embeddedCopies;
        };

        // A row is identified by its MD5 where the dump was hashed here, and by its
        // published SHA-1 or SHA-256 where it was catalogued without one. Never by more
        // than one, so a revision cannot be recognized under two identities.
        bool matchesRaw(const RomRegistryEntry& _entry, const HashCacheEntry& _hashes)
        {
            if (_entry.hash.isValid())
                return _hashes.hasRaw && _entry.hash == _hashes.raw;
            if (_entry.sha1.isValid())
                return _hashes.hasSha1 && _entry.sha1 == _hashes.sha1;
            return _entry.sha256.isValid() && _hashes.hasSha256 && _entry.sha256 == _hashes.sha256;
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
                           const HashPlan& _plan)
        {
            const auto modificationTime = baseLib::filesystem::getFileModificationTime(_file);

            {
                const std::lock_guard lock(hashCacheMutex());
                const auto it = hashCache().find(_file);
                if (it != hashCache().end() && it->second.size == _size &&
                    it->second.modificationTime == modificationTime && (!_plan.raw || it->second.hasRaw) &&
                    (!_plan.swapped || it->second.hasSwapped) &&
                    (!_plan.sha1 || it->second.hasSha1) && (!_plan.sha256 || it->second.hasSha256))
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
            _result.hasRaw = _plan.raw;
            if (_plan.raw)
                _result.raw = baseLib::MD5(data);
            _result.embeddedCopies.clear();
            // Oversized EPROM reads can contain damaged mirrors followed by
            // intact copies. Examine every 1 MiB bank in 2/4 MiB dumps; never
            // assume the first copy (or a filename-specific offset) is correct.
            // Only banks whose signature allows a Pro control ROM are hashed.
            if (isProEpromRead(_size))
            {
                std::set<const RomRegistryEntry*> found;
                for (size_t offset = 0; offset < _size; offset += Sc88ProRomSet::FirmwareSize)
                {
                    const auto bankHead = headOf(data.data() + offset);
                    std::vector<uint8_t> copy;
                    for (const bool swapped : {false, true})
                    {
                        if (!proControlAllows(bankHead, swapped))
                            continue;
                        copy.assign(data.begin() + offset, data.begin() + offset + Sc88ProRomSet::FirmwareSize);
                        if (swapped)
                            swapWords(copy);
                        const auto* entry = findRegistryEntry(copy.size(), baseLib::MD5(copy), swapped);
                        if (entry && isProControl(*entry) && found.insert(entry).second)
                        {
                            FoundRom rom;
                            rom.entry = entry;
                            rom.path = _file;
                            rom.needsWordSwap = swapped;
                            rom.fileOffset = offset;
                            rom.embedded = true;
                            _result.embeddedCopies.push_back(std::move(rom));
                        }
                    }
                }
            }
            for (const auto& mirrored : g_romMirroredSlots)
            {
                if (_size != mirrored.dumpSize || !mirroredAllows(mirrored, headOf(data.data())) ||
                    !isRepeated(data, mirrored.imageSize))
                    continue;
                const baseLib::MD5 copy(data.data(), static_cast<uint32_t>(mirrored.imageSize));
                for (const auto& entry : g_romRegistry)
                {
                    if (!isMirroredImage(entry, mirrored) || !entry.hash.isValid() || entry.hash != copy)
                        continue;
                    FoundRom rom;
                    rom.entry = &entry;
                    rom.path = _file;
                    rom.embedded = true;
                    _result.embeddedCopies.push_back(std::move(rom));
                }
            }
            // Before any swapping: a published digest always describes the file as it
            // is on disk.
            _result.hasSha1 = _plan.sha1;
            if (_plan.sha1)
                _result.sha1 = baseLib::SHA1(data);
            _result.hasSha256 = _plan.sha256;
            if (_plan.sha256)
                _result.sha256 = baseLib::SHA256(data);

            _result.hasSwapped = _plan.swapped;
            if (_plan.swapped)
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
        std::vector<FoundRom> embeddedCopies;

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
                if (!hashCandidate(hashes, path, size, fullPlan(size, spec.normalizeH8Words)))
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

            // Size first, then the signature, and only then the whole file
            const auto plan = planHashes(path, size);
            if (!plan.any())
                continue;

            HashCacheEntry hashes;
            if (!hashCandidate(hashes, path, size, plan))
                continue;
            embeddedCopies.insert(embeddedCopies.end(), hashes.embeddedCopies.begin(), hashes.embeddedCopies.end());

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
        // After the standalone dumps, which win when both hold the same image.
        for (auto& rom : embeddedCopies)
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
