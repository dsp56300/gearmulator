#pragma once

// Standard MIDI File reader with a shared tempo map and stable same-tick order.
// MIDI-port metadata selects the input; unmarked files use PartA/PartB track names.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace sc88smf
{
	struct Event
	{
		double seconds = 0.0;
		std::vector<uint8_t> bytes;
		uint8_t port = 0;		// 0..3, folded by boards with fewer physical inputs
	};

	namespace detail
	{
		struct Reader
		{
			const uint8_t* data = nullptr;
			size_t size = 0;
			size_t pos = 0;

			bool eof() const { return pos >= size; }
			size_t left() const { return pos < size ? size - pos : 0; }

			uint8_t u8() { return pos < size ? data[pos++] : uint8_t(0); }

			uint32_t u16() { const uint32_t a = u8(); return (a << 8) | u8(); }

			uint32_t u32() { const uint32_t a = u16(); return (a << 16) | u16(); }

			// Variable-length quantity, at most four bytes as per the spec.
			uint32_t varLen()
			{
				uint32_t value = 0;
				for(int i = 0; i < 4; ++i)
				{
					const uint8_t b = u8();
					value = (value << 7) | (b & 0x7f);
					if(!(b & 0x80))
						break;
				}
				return value;
			}
		};

		// One event as it sits in a track, before the tempo map is applied.
		struct RawEvent
		{
			uint64_t tick = 0;
			uint32_t order = 0;		// keeps same-tick events in file order
			uint8_t port = 0;
			uint32_t tempo = 0;		// nonzero: a set-tempo meta, not a wire event
			std::vector<uint8_t> bytes;
			uint16_t track = 0;		// only needed to re-port a file that marked none
		};

		// Recognize PartA/PartB as inputs A/B when the file has no port metadata.
		// The letter must stand alone: "Part Bass" is not an input marker.
		inline int portFromTrackName(const std::string& _name)
		{
			std::string name;
			name.reserve(_name.size());
			for(const auto c : _name)
			{
				const auto u = static_cast<unsigned char>(c);
				name.push_back(static_cast<char>(u >= 'A' && u <= 'Z' ? u + ('a' - 'A') : u));
			}

			const auto begin = name.find_first_not_of(" \t");
			if(begin == std::string::npos)
				return -1;
			if(name.compare(begin, 4, "part") != 0 && name.compare(begin, 4, "port") != 0)
				return -1;

			const auto i = name.find_first_not_of(" \t", begin + 4);
			if(i == std::string::npos)
				return -1;
			const auto half = name[i];
			if(half != 'a' && half != 'b')
				return -1;
			if(i + 1 < name.size() && name[i + 1] >= 'a' && name[i + 1] <= 'z')
				return -1;
			return half == 'b' ? 1 : 0;
		}
	}

	// Reads _path into _events, sorted by time. Returns false and fills _error
	// if the file is not a readable SMF.
	inline bool read(const std::string& _path, std::vector<Event>& _events, std::string& _error)
	{
		_events.clear();

		std::vector<uint8_t> file;
		{
			std::FILE* f = std::fopen(_path.c_str(), "rb");
			if(!f)
			{
				_error = "cannot open '" + _path + "'";
				return false;
			}
			std::fseek(f, 0, SEEK_END);
			const long length = std::ftell(f);
			std::fseek(f, 0, SEEK_SET);
			if(length > 0)
			{
				file.resize(static_cast<size_t>(length));
				if(std::fread(file.data(), 1, file.size(), f) != file.size())
					file.clear();
			}
			std::fclose(f);
		}
		if(file.size() < 14)
		{
			_error = "'" + _path + "' is too short to be a MIDI file";
			return false;
		}

		detail::Reader r{file.data(), file.size(), 0};
		if(r.u32() != 0x4d546864)	// "MThd"
		{
			_error = "'" + _path + "' is not a Standard MIDI File (no MThd)";
			return false;
		}
		const uint32_t headerLength = r.u32();
		r.u16();								// format, only the division matters here
		const uint32_t trackCount = r.u16();
		const uint32_t division = r.u16();
		if(headerLength > 6)
			r.pos += headerLength - 6;

		// Tick -> seconds. A positive division is ticks per quarter note and
		// scales with tempo; a negative one is SMPTE and is absolute.
		const bool smpte = (division & 0x8000) != 0;
		double smpteSecondsPerTick = 0.0;
		if(smpte)
		{
			const auto framesPerSecond = static_cast<double>(256 - (division >> 8));
			const auto ticksPerFrame = static_cast<double>(division & 0xff);
			if(framesPerSecond <= 0.0 || ticksPerFrame <= 0.0)
			{
				_error = "'" + _path + "' has an invalid SMPTE division";
				return false;
			}
			smpteSecondsPerTick = 1.0 / (framesPerSecond * ticksPerFrame);
		}
		else if(!division)
		{
			_error = "'" + _path + "' has a zero tick division";
			return false;
		}

		std::vector<detail::RawEvent> raw;
		std::vector<std::string> trackNames;
		bool fileMarksPorts = false;
		uint32_t order = 0;

		for(uint32_t track = 0; track < trackCount && !r.eof(); ++track)
		{
			// Skip any chunk that is not a track, as the spec requires.
			uint32_t id = r.u32();
			uint32_t chunkLength = r.u32();
			while(id != 0x4d54726b && !r.eof())	// "MTrk"
			{
				r.pos += chunkLength;
				if(r.left() < 8)
					break;
				id = r.u32();
				chunkLength = r.u32();
			}
			if(id != 0x4d54726b)
				break;

			const size_t trackEnd = std::min(r.pos + chunkLength, r.size);
			uint64_t tick = 0;
			uint8_t runningStatus = 0;
			uint8_t currentPort = 0;
			std::string trackName;

			while(r.pos < trackEnd)
			{
				tick += r.varLen();

				uint8_t status = r.u8();
				if(status < 0x80)
				{
					// Running status: the byte just read is the first data byte.
					if(!runningStatus)
						break;			// no status to run from — track is corrupt
					--r.pos;
					status = runningStatus;
				}

				if(status == 0xff)			// meta
				{
					const uint8_t type = r.u8();
					const uint32_t length = r.varLen();
					const size_t body = r.pos;
					if(type == 0x51 && length >= 3)			// set tempo
					{
						const uint32_t tempo = (uint32_t(file[body]) << 16) |
						                       (uint32_t(file[body + 1]) << 8) | file[body + 2];
						if(tempo)
							raw.push_back({tick, order++, currentPort, tempo, {}, static_cast<uint16_t>(track)});
					}
					else if(type == 0x21 && length >= 1)	// midi port
					{
						currentPort = static_cast<uint8_t>(std::min<uint8_t>(file[body], 3));
						fileMarksPorts = true;
					}
					else if(type == 0x03 && trackName.empty())	// track name
					{
						const size_t nameEnd = std::min(body + length, trackEnd);
						trackName.assign(file.begin() + body, file.begin() + nameEnd);
					}
					r.pos = std::min(body + length, trackEnd);
					if(type == 0x2f)						// end of track
						break;
					continue;
				}

				if(status == 0xf0 || status == 0xf7)		// sysex / escape
				{
					const uint32_t length = r.varLen();
					const size_t body = r.pos;
					const size_t end = std::min(body + length, trackEnd);
					std::vector<uint8_t> bytes;
					if(status == 0xf0)
						bytes.push_back(0xf0);				// the F0 is implied by the event type
					bytes.insert(bytes.end(), file.begin() + body, file.begin() + end);
					if(!bytes.empty())
						raw.push_back({tick, order++, currentPort, 0, std::move(bytes), static_cast<uint16_t>(track)});
					r.pos = end;
					runningStatus = 0;						// sysex cancels running status
					continue;
				}

				// Channel voice message: one or two data bytes.
				runningStatus = status;
				const uint8_t high = status & 0xf0;
				const size_t dataBytes = (high == 0xc0 || high == 0xd0) ? 1 : 2;
				std::vector<uint8_t> bytes;
				bytes.push_back(status);
				for(size_t i = 0; i < dataBytes; ++i)
					bytes.push_back(r.u8());
				raw.push_back({tick, order++, currentPort, 0, std::move(bytes), static_cast<uint16_t>(track)});
			}

			r.pos = trackEnd;
			trackNames.emplace_back(std::move(trackName));
		}

		// Nothing marked a port, so fall back to the name convention - and only
		// when the file names both halves, which keeps a lone "Part A" in an
		// ordinary single-port file from moving anything.
		if(!fileMarksPorts)
		{
			std::vector<uint8_t> namedPorts(trackNames.size(), 0);
			bool namesA = false, namesB = false;
			for(size_t i = 0; i < trackNames.size(); ++i)
			{
				const auto port = detail::portFromTrackName(trackNames[i]);
				if(port < 0)
					continue;
				namedPorts[i] = static_cast<uint8_t>(port);
				namesA |= port == 0;
				namesB |= port == 1;
			}
			if(namesA && namesB)
				for(auto& e : raw)
					if(e.track < namedPorts.size())
						e.port = namedPorts[e.track];
		}

		if(raw.empty())
		{
			_error = "'" + _path + "' contains no MIDI events";
			return false;
		}

		std::sort(raw.begin(), raw.end(), [](const detail::RawEvent& _a, const detail::RawEvent& _b)
		{
			if(_a.tick != _b.tick)
				return _a.tick < _b.tick;
			return _a.order < _b.order;
		});

		// Tempo changes come from any track and apply globally, so the whole
		// merged list is walked against one running tempo.
		uint32_t tempo = 500000;			// MIDI default, 120 bpm
		double seconds = 0.0;
		uint64_t prevTick = 0;

		for(const auto& e : raw)
		{
			const auto deltaTicks = static_cast<double>(e.tick - prevTick);
			prevTick = e.tick;
			seconds += smpte ? deltaTicks * smpteSecondsPerTick
			                 : deltaTicks * (double(tempo) * 1e-6) / double(division);

			if(e.tempo)
			{
				tempo = e.tempo;
				continue;
			}

			// Kept as-is up to the four ports the SC-8850's USB provides; each
			// board adapter folds the number onto the inputs it actually has.
			_events.push_back({seconds, e.bytes, e.port});
		}

		if(_events.empty())
		{
			_error = "'" + _path + "' contains no playable MIDI events";
			return false;
		}
		return true;
	}
}
