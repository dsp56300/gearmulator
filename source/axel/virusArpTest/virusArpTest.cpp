// Plays the Virus firmware's arpeggiator and the C++ one side by side and compares what they play, note by note: every
// preset pattern, the TI patch's own pattern, the modes and octave ranges, hold, keys that change while the arpeggio
// runs, Note Length, Swing, Arp Clock and Clock Tempo. The firmware tells what its arpeggiator plays with Midi Arp Send.
// A TI firmware or an A, B or C ROM
//
// virusArpTest <rom file> [part of a case name]
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "virusLib/device.h"
#include "virusLib/microcontroller.h"
#include "virusLib/romloader.h"

#include "baseLib/os.h"

namespace
{
	using namespace virusLib;
	using namespace synthLib;

	float g_samplerate = 44100.0f;	// the device's, once it runs
	bool g_ti = true;				// a TI firmware, else an A, B or C
	double g_stepScale = 1.0;		// the A's arpeggiator runs at half the rate
	constexpr uint32_t g_blockSize = 16;

	// The firmware places its notes on its own clock, which runs in blocks of 64 samples, sometimes two late. That and a
	// block of ours is how far a note may be from where the C++ arpeggiator has it. A tick of the firmware's
	// arpeggiator is 10 ms at 120 BPM
	constexpr int64_t g_tolerance = 150;

	// Where a TI patch keeps its arpeggiator. The firmware loads Arp Mode from page B 1, 15 is the live parameter
	constexpr size_t g_arpMode = 128 + 1;
	constexpr size_t g_arpPattern = 128 + 2;
	constexpr size_t g_arpOctaves = 128 + 3;
	constexpr size_t g_arpHold = 128 + 4;
	constexpr size_t g_arpNoteLength = 128 + 5;
	constexpr size_t g_arpSwing = 128 + 6;
	constexpr size_t g_arpModeLive = 128 + 15;
	constexpr size_t g_clockTempo = 128 + 16;
	constexpr size_t g_arpClock = 128 + 17;
	constexpr size_t g_userPatternLength = 256 + 127;
	constexpr size_t g_userPattern = 384;

	// Arp Clock in 1/384 of a whole note, see virusLib::Arpeggiator
	constexpr double g_clockUnits[] = {3, 3, 6, 12, 24, 48, 96, 9, 18, 36, 72, 8, 16, 32, 64, 128, 144, 192};

	struct Key
	{
		uint64_t time;		// in samples from the start of the case
		uint8_t note;
		uint8_t velocity;	// 0 releases the key
	};

	struct Note
	{
		int64_t time;
		bool on;
		uint8_t note;
		uint8_t velocity;
	};

	struct Case
	{
		std::string name;
		ROMFile::TPreset patch{};
		std::vector<Key> keys;
		uint64_t length = 0;	// in samples
		bool midiClock = false;	// the firmware follows MIDI clock, else it runs on its own. The C++ arpeggiator runs
								// on a grid where the firmware has its steps, see findGrid()
		bool steady = false;	// the firmware cannot start it on a grid: compared from the third cycle of notes on,
								// wherever the firmware has them
	};

	// MIDI clock runs this many steps before a case starts, so that the firmware has locked to it
	constexpr double g_clockLock = 16;

	double stepSamples(const ROMFile::TPreset& _patch)
	{
		const auto bpm = static_cast<double>(_patch[g_clockTempo] + 63);
		const auto units = g_clockUnits[std::min<size_t>(_patch[g_arpClock], std::size(g_clockUnits) - 1)];
		return g_samplerate * 60.0 / bpm * 4.0 * units / 384.0 * g_stepScale;
	}

	// A tick of the firmware's arpeggiator, 1/192 note
	double tickSamples(const ROMFile::TPreset& _patch)
	{
		return g_samplerate * 60.0 / (_patch[g_clockTempo] + 63) / 48.0;
	}

	// How far a note may be from where the C++ arpeggiator has it. With MIDI clock up to a tick: the C, for one, moves
	// its steps by most of one when a key joins a chord
	int64_t tolerance(const Case& _case)
	{
		return _case.midiClock ? std::max(g_tolerance, static_cast<int64_t>(tickSamples(_case.patch))) : g_tolerance;
	}

	// Where comparing starts: for a case that is steady only then, after two cycles of three notes, four steps each
	int64_t compareFrom(const Case& _case)
	{
		return _case.steady ? static_cast<int64_t>(8 * stepSamples(_case.patch)) : 0;
	}

	bool isRandom(const Case& _case)
	{
		return _case.patch[g_arpMode] == 5;
	}

	SMidiEvent keyEvent(const Key& _key, const uint32_t _offset)
	{
		const auto status = static_cast<uint8_t>(_key.velocity ? M_NOTEON : M_NOTEOFF);
		return {MidiEventSource::Host, status, _key.note, _key.velocity ? _key.velocity : static_cast<uint8_t>(64), _offset};
	}

	void addNote(std::vector<Note>& _notes, const SMidiEvent& _e, const int64_t _time)
	{
		const auto status = _e.a & 0xf0;

		if (!_e.sysex.empty() || (status != M_NOTEON && status != M_NOTEOFF))
			return;

		_notes.push_back({_time, status == M_NOTEON && _e.c > 0, _e.b, _e.c});
	}

	class Firmware
	{
	public:
		// _hostTransport: the device gets the host's transport, which makes it play the arpeggio in C++
		Firmware(const std::vector<uint8_t>& _romData, const std::string& _romName, const DeviceModel _model,
			const bool _hostTransport = false)
			: m_rom(_romData, _romName, _model)
			, m_hostTransport(_hostTransport)
		{
			DeviceCreateParams params;
			params.romData = _romData;
			params.romName = _romName;
			params.customData = static_cast<uint32_t>(_model);
			params.preferredSamplerate = g_samplerate;
			params.hostSamplerate = g_samplerate;

			m_device = std::make_unique<virusLib::Device>(params);
			m_device->setSamplerate(g_samplerate);
			g_samplerate = m_device->getSamplerate();

			for (auto& b : m_outs)
				b.resize(g_blockSize);
			for (auto& b : m_ins)
				b.resize(g_blockSize);

			run(static_cast<uint64_t>(g_samplerate), {});

			// Midi Arp Send: the firmware sends what its arpeggiator plays to MIDI out. Page D on the TI, C before it
			SMidiEvent arpSend(MidiEventSource::Host);
			arpSend.sysex = {0xf0, 0x00, 0x20, 0x33, 0x01, 0x10, static_cast<uint8_t>(g_ti ? 0x73 : 0x72), 0x00, 0x60, 0x01, 0xf7};
			run(441, {{0, arpSend}});
		}

		bool isValid() const { return m_device->isValid() && m_device->getArpeggiator().isValid(); }

		const virusLib::Arpeggiator& getArpeggiator() const { return m_device->getArpeggiator(); }
		const ROMFile& getRom() const { return m_rom; }

		// Sends _events, at their sample from now, and returns what the device sends back
		std::vector<SMidiEvent> send(const std::vector<std::pair<uint64_t, SMidiEvent>>& _events, const uint64_t _samples)
		{
			std::vector<SMidiEvent> out;
			run(_samples, _events, nullptr, &out);
			return out;
		}

		SysexBuffer requestSingle()
		{
			SMidiEvent request(MidiEventSource::Host);
			request.sysex = {0xf0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x40, 0xf7};
			for (const auto& e : send({{0, request}}, 4410))
			{
				if (e.sysex.size() > 9 && e.sysex[6] == 0x10)
					return e.sysex;
			}
			return {};
		}

		std::vector<Note> play(const Case& _case)
		{
			// The patch goes to the edit buffer in single mode, and gets a moment to arrive
			SMidiEvent dump(MidiEventSource::Host);
			dump.sysex = Microcontroller::createSingleDump(m_rom, BankNumber::EditBuffer, SINGLE, _case.patch);
			run(8820, {{0, dump}});

			if (_case.midiClock)
			{
				// Start, and a while to lock to the clock. The case starts on a step
				const auto step = stepSamples(_case.patch);
				m_clockInterval = g_samplerate * 60.0 / (_case.patch[g_clockTempo] + 63) / 24.0;
				m_nextTick = static_cast<double>(m_time);
				run(static_cast<uint64_t>(std::round(step * g_clockLock)), {{0, SMidiEvent(MidiEventSource::Host, M_START)}});
			}

			std::vector<std::pair<uint64_t, SMidiEvent>> events;
			for (const auto& k : _case.keys)
				events.emplace_back(k.time, keyEvent(k, 0));

			std::vector<Note> notes;
			run(_case.length, events, &notes);

			// Every key up and the arpeggio stopped before the next case, hold or not
			events.clear();
			for (const auto& k : _case.keys)
				events.emplace_back(0, keyEvent({0, k.note, 0}, 0));
			events.emplace_back(0, SMidiEvent(MidiEventSource::Host, M_CONTROLCHANGE, MC_ALLNOTESOFF, 0));
			if (_case.midiClock)
				events.emplace_back(0, SMidiEvent(MidiEventSource::Host, M_STOP));
			run(13230, events);
			m_clockInterval = 0.0;

			return notes;
		}

	private:
		// _events at their sample from now
		void run(const uint64_t _samples, const std::vector<std::pair<uint64_t, SMidiEvent>>& _events,
			std::vector<Note>* _notes = nullptr, std::vector<SMidiEvent>* _out = nullptr)
		{
			TAudioInputs ins{};
			TAudioOutputs outs{};
			for (size_t i = 0; i < ins.size(); ++i)
				ins[i] = m_ins[i].data();
			for (size_t i = 0; i < outs.size(); ++i)
				outs[i] = m_outs[i].data();

			std::vector<SMidiEvent> in, out;

			for (uint64_t t = 0; t < _samples; t += g_blockSize)
			{
				in.clear();
				out.clear();

				for (const auto& [time, e] : _events)
				{
					if (time < t || time >= t + g_blockSize)
						continue;
					in.push_back(e);
					in.back().offset = static_cast<uint32_t>(time - t);
				}

				// MIDI clock, from where the last block left it
				while (m_clockInterval > 0.0 && m_nextTick < static_cast<double>(m_time + g_blockSize))
				{
					const auto at = std::max(m_nextTick, static_cast<double>(m_time));
					in.emplace_back(MidiEventSource::Host, M_TIMINGCLOCK, 0, 0, static_cast<uint32_t>(at - static_cast<double>(m_time)));
					m_nextTick += m_clockInterval;
				}
				std::stable_sort(in.begin(), in.end(), [](const SMidiEvent& _a, const SMidiEvent& _b) { return _a.offset < _b.offset; });

				if (m_hostTransport)
				{
					// A host that does not play, at the tempo of the patches
					SMidiEvent transport(MidiEventSource::Internal);
					transport.type = MidiEventType::HostTransport;
					transport.transport = {0.0, 120.0f, false, false};
					in.insert(in.begin(), transport);
				}

				m_device->process(ins, outs, g_blockSize, in, out);

				for (const auto& e : out)
				{
					if (_notes)
						addNote(*_notes, e, static_cast<int64_t>(t + e.offset));
					if (_out)
						_out->push_back(e);
				}

				m_time += g_blockSize;
			}
		}

		ROMFile m_rom;
		const bool m_hostTransport;
		uint64_t m_time = 0;			// samples since the device started
		double m_clockInterval = 0.0;	// samples from one MIDI clock to the next, 0 for none
		double m_nextTick = 0.0;		// like m_time
		std::unique_ptr<virusLib::Device> m_device;
		std::array<std::vector<float>, 12> m_outs;
		std::array<std::vector<float>, 4> m_ins;
	};

	// _grid: where the firmware's clock has a step, in samples from the start of the case. Without one, the C++
	// arpeggiator runs on its own and starts with the key
	std::vector<Note> playCpp(const virusLib::Arpeggiator& _arpeggiator, const Case& _case, const double* _grid = nullptr)
	{
		virusLib::Arpeggiator arp = _arpeggiator;

		std::array<const ROMFile::TPreset*, 16> singles{};
		singles[0] = &_case.patch;

		const auto bpm = static_cast<float>(_case.patch[g_clockTempo] + 63);
		const auto step = stepSamples(_case.patch);
		const auto quartersPerSample = bpm / 60.0 / g_samplerate;
		const auto stepQuarters = step * quartersPerSample;

		std::vector<Note> notes;
		std::vector<SMidiEvent> in, out;

		for (uint64_t t = 0; t < _case.length; t += g_blockSize)
		{
			in.clear();
			out.clear();

			if (_grid)
			{
				// A host that plays, with a step of its grid where the firmware has one
				SMidiEvent transport(MidiEventSource::Internal);
				transport.type = MidiEventType::HostTransport;
				transport.transport = {stepQuarters * 1000.0 + (static_cast<double>(t) - *_grid) * quartersPerSample, bpm, true, true};
				in.push_back(transport);
			}

			for (const auto& k : _case.keys)
			{
				if (k.time >= t && k.time < t + g_blockSize)
					in.push_back(keyEvent(k, static_cast<uint32_t>(k.time - t)));
			}

			arp.process(singles, in, out, g_blockSize, g_samplerate);

			for (const auto& e : out)
				addNote(notes, e, static_cast<int64_t>(t + e.offset));
		}

		// The firmware ends no note sooner than a block of its clock after it began, 64 samples
		std::array<int64_t, 128> started{};
		for (auto& n : notes)
		{
			if (n.on)
				started[n.note] = n.time;
			else
				n.time = std::max(n.time, started[n.note] + 64);
		}
		std::stable_sort(notes.begin(), notes.end(), [](const Note& _a, const Note& _b) { return _a.time < _b.time; });

		return notes;
	}

	// A note on without velocity ends the note if it sounds, else nothing happens, as a note off for a note that
	// does not sound. The firmware sends those for steps without velocity
	std::vector<Note> sounding(const std::vector<Note>& _notes)
	{
		std::vector<Note> result;
		std::array<int, 128> playing{};

		for (const auto& n : _notes)
		{
			if (n.on)
			{
				++playing[n.note];
				result.push_back(n);
			}
			else if (playing[n.note] > 0)
			{
				--playing[n.note];
				result.push_back(n);
			}
		}

		return result;
	}

	struct Result
	{
		bool ok = true;
		std::string text;
	};

	std::string describe(const Note& _n, const double _step)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%s %d v%d @%.3f", _n.on ? "on" : "off", _n.note, _n.velocity,
			static_cast<double>(_n.time) / _step);
		return buf;
	}

	bool matches(const Case& _case, const Note& _a, const Note& _b)
	{
		// Random: the rhythm counts, and the velocity
		return _a.on == _b.on && (isRandom(_case) || _a.note == _b.note) && (!_a.on || _a.velocity == _b.velocity);
	}

	// How much later the firmware plays than the C++ arpeggiator: the offset that matches most notes after the first
	// step, in the middle of how far they are apart then
	int64_t findShift(const Case& _case, const std::vector<Note>& _fw, const std::vector<Note>& _cpp)
	{
		const auto step = stepSamples(_case.patch);
		const auto from = compareFrom(_case);

		auto afterFirst = [&](const std::vector<Note>& _v)
		{
			std::vector<Note> r;
			for (const auto& n : _v)
			{
				if (n.on && static_cast<double>(n.time) > _v.front().time + step * 0.25 && n.time >= from)
					r.push_back(n);
			}
			return r;
		};

		if (_fw.empty() || _cpp.empty())
			return 0;

		const auto fw = afterFirst(_fw);
		const auto cpp = afterFirst(_cpp);

		auto count = [&](const int64_t _shift)
		{
			size_t c = 0;
			for (const auto& n : cpp)
			{
				c += std::any_of(fw.begin(), fw.end(), [&](const Note& _f)
				{
					return matches(_case, _f, n) && std::abs(_f.time - _shift - n.time) <= tolerance(_case);
				});
			}
			return c;
		};

		int64_t shift = 0;
		size_t best = 0;

		for (size_t i = 0; i < fw.size() && i < 8; ++i)
		{
			for (size_t j = 0; j < cpp.size() && j < 8; ++j)
			{
				if (!matches(_case, fw[i], cpp[j]))
					continue;
				const auto candidate = fw[i].time - cpp[j].time;
				const auto c = count(candidate);
				if (c > best || (c == best && std::abs(candidate) < std::abs(shift)))
				{
					best = c;
					shift = candidate;
				}
			}
		}

		int64_t lo = std::numeric_limits<int64_t>::max(), hi = std::numeric_limits<int64_t>::min();
		for (const auto& n : cpp)
		{
			for (const auto& f : fw)
			{
				const auto d = f.time - shift - n.time;
				if (!matches(_case, f, n) || std::abs(d) > tolerance(_case))
					continue;
				lo = std::min(lo, d);
				hi = std::max(hi, d);
			}
		}

		return lo <= hi ? shift + (lo + hi) / 2 : 0;
	}

	// Where the firmware's clock has its steps, relative to the C++ arpeggiator's that starts with the key
	double findGrid(const Case& _case, const std::vector<Note>& _fw, const std::vector<Note>& _cpp)
	{
		if (_fw.empty() || _cpp.empty())
			return 0.0;

		const auto step = stepSamples(_case.patch);

		auto phase = std::fmod(static_cast<double>(_cpp.front().time + findShift(_case, _fw, _cpp)), step);
		if (phase < 0)
			phase += step;

		// On their own clock, the A, B and C start it with the key, at their next tick. Notes that come a little early
		// put the grid a little before the key, which would make the key late for the C++ arpeggiator
		if (!g_ti && !_case.midiClock && !_case.keys.empty())
		{
			const auto key = std::fmod(static_cast<double>(_case.keys.front().time), step);
			if (const auto late = std::fmod(key - phase + step, step); late > 0.0 && late < g_tolerance)
				return key;
		}
		return phase;
	}

	// A key struck after a step started plays that step at once, and the note ends where it would have. When that is
	// less than a tick of the firmware's arpeggiator after the key, the firmware ends it a few milliseconds later, at a
	// point that is not known. Those ends are not compared
	void skipShortFirstNotes(const Case& _case, std::vector<Note>& _fw, std::vector<Note>& _cpp)
	{
		if (_cpp.empty() || !_cpp.front().on)
			return;

		const auto tick = static_cast<int64_t>(tickSamples(_case.patch));
		const auto first = _cpp.front().time;

		for (size_t i = 0; i < _cpp.size() && _cpp[i].on && _cpp[i].time == first; ++i)
		{
			const auto note = _cpp[i].note;
			auto isEnd = [&](const Note& _n) { return !_n.on && _n.note == note; };

			const auto c = std::find_if(_cpp.begin(), _cpp.end(), isEnd);
			if (c == _cpp.end() || c->time - first >= tick)
				continue;
			_cpp.erase(c);

			const auto f = std::find_if(_fw.begin(), _fw.end(), isEnd);
			if (f != _fw.end())
				_fw.erase(f);
		}
	}

	// Where a key starts a new set of notes with hold
	std::vector<int64_t> holdRestarts(const Case& _case)
	{
		std::vector<int64_t> restarts;
		if (!_case.patch[g_arpHold])
			return restarts;

		std::set<uint8_t> down;
		bool held = false;
		for (const auto& k : _case.keys)
		{
			if (!k.velocity)
			{
				down.erase(k.note);
				continue;
			}
			if (down.empty() && held)
				restarts.push_back(static_cast<int64_t>(k.time));
			down.insert(k.note);
			held = true;
		}
		return restarts;
	}

	// Both have to play the same notes at the same times, from the first key on
	Result compare(const Case& _case, std::vector<Note> _fw, std::vector<Note> _cpp, const double _grid)
	{
		const auto step = stepSamples(_case.patch);

		skipShortFirstNotes(_case, _fw, _cpp);

		// A hold restart on the A, B and C ends the notes that sound, their audio stops at the key. Their MIDI out never
		// says so, those ends are not compared
		if (!g_ti)
		{
			for (const auto r : holdRestarts(_case))
			{
				_cpp.erase(std::remove_if(_cpp.begin(), _cpp.end(), [&](const Note& _n)
				{
					return !_n.on && _n.time >= r && _n.time <= r + 64;
				}), _cpp.end());
			}
		}

		Result r;

		const auto from = compareFrom(_case);

		auto window = [&](const int64_t _t, const int64_t _margin)
		{
			return _t >= from + _margin && _t <= static_cast<int64_t>(_case.length) - static_cast<int64_t>(step) - _margin;
		};

		std::vector<bool> fwUsed(_fw.size(), false);
		size_t matched = 0;
		int64_t worst = 0;
		std::vector<std::string> missing, extra;

		for (const auto& c : _cpp)
		{
			if (!window(c.time, 0))
				continue;

			size_t found = _fw.size();
			int64_t deviation = 0;

			for (size_t i = 0; i < _fw.size(); ++i)
			{
				const auto d = _fw[i].time - c.time;
				if (fwUsed[i] || !matches(_case, _fw[i], c) || std::abs(d) > tolerance(_case))
					continue;
				if (found == _fw.size() || std::abs(d) < std::abs(deviation))
				{
					found = i;
					deviation = d;
				}
			}

			if (found == _fw.size())
			{
				missing.push_back(describe(c, step));
				continue;
			}

			fwUsed[found] = true;
			++matched;
			worst = std::max(worst, std::abs(deviation));
		}

		for (size_t i = 0; i < _fw.size(); ++i)
		{
			if (!fwUsed[i] && window(_fw[i].time, tolerance(_case)))
				extra.push_back(describe(_fw[i], step));
		}

		if (isRandom(_case))
		{
			// Any held note in the range, in any order
			std::set<int> allowed;
			for (const auto& k : _case.keys)
			{
				for (int o = 0; o <= (_case.patch[g_arpOctaves] & 3); ++o)
					allowed.insert(k.note + 12 * o);
			}
			for (const auto& v : {&_fw, &_cpp})
			{
				for (const auto& n : *v)
				{
					if (n.on && !allowed.count(n.note))
					{
						r.ok = false;
						r.text += "    random note " + std::to_string(n.note) + " was never held\n";
					}
				}
			}
		}

		auto list = [](const std::vector<std::string>& _v)
		{
			std::string s;
			for (size_t i = 0; i < _v.size() && i < 12; ++i)
				s += (i ? ", " : "") + _v[i];
			if (_v.size() > 12)
				s += ", ... " + std::to_string(_v.size()) + " in all";
			return s;
		};

		if (!missing.empty())
		{
			r.ok = false;
			r.text += "    only in C++: " + list(missing) + "\n";
		}
		if (!extra.empty())
		{
			r.ok = false;
			r.text += "    only in the firmware: " + list(extra) + "\n";
		}
		if (_cpp.empty() != _fw.empty())
		{
			r.ok = false;
			r.text += _fw.empty() ? "    the firmware played nothing\n" : "    the C++ arpeggiator played nothing\n";
		}

		char buf[160];
		std::snprintf(buf, sizeof(buf), "    %zu notes, the firmware's clock %.1f ms into a step, at most %.2f ms apart\n",
			matched, _grid * 1000.0 / g_samplerate, static_cast<double>(worst) * 1000.0 / g_samplerate);
		r.text = buf + r.text;

		if (!r.ok)
		{
			auto timeline = [&](const char* _title, const std::vector<Note>& _v)
			{
				r.text += std::string("    ") + _title + ":";
				size_t count = 0;
				for (const auto& n : _v)
				{
					if (!n.on)
						continue;
					char b[32];
					std::snprintf(b, sizeof(b), " %d@%.3f", n.note, static_cast<double>(n.time) / step);
					r.text += b;
					if (++count == 40)
						break;
				}
				r.text += "\n";
			};
			timeline("firmware", _fw);
			timeline("C++     ", _cpp);
		}

		return r;
	}

	// The cases. Every one holds its keys for _steps steps of its clock
	class Cases
	{
	public:
		explicit Cases(const ROMFile::TPreset& _base) : m_base(_base)
		{
			// Up, one octave, no hold, Note Length 100%, no swing, 120 BPM, 1/16
			m_base[g_arpMode] = 1;
			m_base[g_arpPattern] = 1;
			m_base[g_arpOctaves] = 0;
			m_base[g_arpHold] = 0;
			m_base[g_arpNoteLength] = 64;
			m_base[g_arpSwing] = 0;
			m_base[g_clockTempo] = 57;
			m_base[g_arpClock] = 4;
		}

		ROMFile::TPreset base() const { return m_base; }

		Case& add(const std::string& _name, const ROMFile::TPreset& _patch, std::vector<Key> _keys, const double _steps,
			const bool _midiClock = false)
		{
			std::sort(_keys.begin(), _keys.end(), [](const Key& _a, const Key& _b) { return _a.time < _b.time; });
			Case c;
			c.name = _name;
			c.patch = _patch;
			if (g_ti)
				c.patch[g_arpModeLive] = c.patch[g_arpMode];

			// A byte with the top bit set breaks the dump, and with it every patch that follows
			for (const auto b : c.patch)
			{
				if (b > 127)
				{
					std::printf("case '%s' has a patch byte above 127\n", _name.c_str());
					std::abort();
				}
			}
			c.keys = std::move(_keys);
			c.length = static_cast<uint64_t>(_steps * stepSamples(_patch));
			c.midiClock = _midiClock;
			return m_cases.emplace_back(std::move(c));
		}

		// C, E and G at once, velocity _velocity
		Case& addChord(const std::string& _name, const ROMFile::TPreset& _patch, const double _steps, const uint8_t _velocity = 100)
		{
			return add(_name, _patch, {{0, 60, _velocity}, {0, 64, _velocity}, {0, 67, _velocity}}, _steps);
		}

		const std::vector<Case>& get() const { return m_cases; }

	private:
		ROMFile::TPreset m_base;
		std::vector<Case> m_cases;
	};

	std::string name(const char* _format, const int _a, const int _b = 0)
	{
		char buf[96];
		std::snprintf(buf, sizeof(buf), _format, _a, _b);
		return buf;
	}

	void createCases(Cases& _cases, const int _patterns)
	{
		// Every preset pattern, two cycles of the longest. On the TI, 0 is the patch's own
		for (int p = g_ti ? 1 : 0; p < _patterns; ++p)
		{
			auto s = _cases.base();
			s[g_arpPattern] = static_cast<uint8_t>(p);
			_cases.addChord(name("pattern %02d", p), s, 70);
		}

		// The pattern velocities move the key's, up to the limits
		for (const uint8_t v : {1, 30, 127})
		{
			for (const int p : {1, 17, 34, 39})
			{
				auto s = _cases.base();
				s[g_arpPattern] = static_cast<uint8_t>(p);
				_cases.addChord(name("pattern %02d, key velocity %d", p, v), s, 40, v);
			}
		}

		// The patch's own pattern, TI only. Per step: length (of 128), velocity, and bit 0 to play it
		auto user = [&](const std::string& _name, const int _count, const auto& _step, const uint8_t _velocity = 100)
		{
			if (!g_ti)
				return;

			auto s = _cases.base();
			s[g_arpPattern] = 0;
			s[g_userPatternLength] = static_cast<uint8_t>(_count - 1);
			for (int i = 0; i < 32; ++i)
			{
				const auto [length, velocity, on] = _step(i);
				s[g_userPattern + i * 3 + 0] = static_cast<uint8_t>(length);
				s[g_userPattern + i * 3 + 1] = static_cast<uint8_t>(velocity);
				s[g_userPattern + i * 3 + 2] = static_cast<uint8_t>(on);
			}
			_cases.addChord(_name, s, 70, _velocity);
		};

		user("user, 16 steps, lengths and velocities", 16, [](int i) { return std::array<int, 3>{(i * 8 + 7) & 127, 127 - i * 4, 1}; });
		user("user, 16 steps, every 4th a rest", 16, [](int i) { return std::array<int, 3>{(i * 8 + 7) & 127, 127 - i * 4, i % 4 != 3}; });
		user("user, 5 steps, rests", 5, [](int i) { return std::array<int, 3>{(40 + i * 20) & 127, (60 + i * 10) & 127, i != 1 && i != 2}; });
		user("user, 32 steps", 32, [](int i) { return std::array<int, 3>{(i * 37) & 127, 1 + ((i * 53) & 127) % 127, (i * 7) % 5 != 0}; });
		user("user, 1 step", 1, [](int) { return std::array<int, 3>{64, 100, 1}; });
		user("user, first step a rest", 8, [](int i) { return std::array<int, 3>{100, 90, i != 0}; });
		user("user, full lengths", 8, [](int i) { return std::array<int, 3>{127, 100, i % 3 != 1}; });
		user("user, zero lengths", 8, [](int) { return std::array<int, 3>{0, 100, 1}; });
		user("user, two rests first", 8, [](int i) { return std::array<int, 3>{60 + i, 90, i > 1}; });
		user("user, rests first and last", 8, [](int i) { return std::array<int, 3>{60 + i, 70 + i, i > 0 && i < 6}; });
		user("user, all rests", 8, [](int) { return std::array<int, 3>{64, 100, 0}; });
		user("user, first step a rest, key velocity 127", 8, [](int i) { return std::array<int, 3>{100, 90, i != 0}; }, 127);
		user("user, a step without velocity", 8, [](int i) { return std::array<int, 3>{64, i == 3 ? 0 : 100, 1}; });
		user("user, a step without velocity, key velocity 127", 8, [](int i) { return std::array<int, 3>{64, i == 3 ? 0 : 100, 1}; }, 127);

		// Modes and octave ranges, keys struck E, C, G
		const char* modes[] = {"", "Up", "Down", "Up&Down", "As Played", "Random", "Chord"};
		for (int mode = 1; mode <= 6; ++mode)
		{
			for (int octaves = 0; octaves < 4; ++octaves)
			{
				auto s = _cases.base();
				s[g_arpMode] = static_cast<uint8_t>(mode);
				s[g_arpOctaves] = static_cast<uint8_t>(octaves);
				// A sixth of a step apart, two ticks of the firmware's arpeggiator or more: the A, B and C start with the
				// tick after the first key, a key closer to it than a tick races that
				const auto apart = static_cast<uint64_t>(stepSamples(s) / 6);
				_cases.add(std::string(modes[mode]) + name(", %d octaves", octaves + 1), s, {{0, 64, 100}, {apart, 60, 100}, {apart * 2, 67, 100}}, 40);
			}
		}

		// Keys that change while the arpeggio runs
		for (int mode = 1; mode <= 4; ++mode)
		{
			auto s = _cases.base();
			s[g_arpMode] = static_cast<uint8_t>(mode);
			s[g_arpOctaves] = 1;
			const auto step = static_cast<uint64_t>(stepSamples(s));
			_cases.add(std::string(modes[mode]) + ", keys change", s, {{0, 60, 100}, {0, 67, 100}, {step * 5 + step / 3, 64, 90},
				{step * 11 + step / 2, 60, 0}, {step * 17 + step / 4, 72, 110}, {step * 23 + step / 2, 67, 0}}, 36);
		}

		// Hold: the notes stay when the keys go up, a key struck with none down starts new ones
		for (int mode = 1; mode <= 6; mode += 5)
		{
			auto s = _cases.base();
			s[g_arpMode] = static_cast<uint8_t>(mode);
			s[g_arpHold] = 1;
			const auto step = static_cast<uint64_t>(stepSamples(s));
			const std::vector<Key> keys = {{0, 60, 100}, {0, 64, 100}, {step * 4 + step / 3, 60, 0},
				{step * 4 + step / 3, 64, 0}, {step * 12 + step / 2 - step / 5, 62, 100}, {step * 13 + step / 3, 65, 100},
				{step * 16 + step / 2, 62, 0}, {step * 18 + step / 2, 65, 0}, {step * 24 + step / 2 + step / 5, 69, 100}};
			// A key that starts a new set with hold starts the pattern over on the A, B and C, and ends the notes that
			// still sound, see compare(). The TI goes on. On their own clock, the A, B and C also start that clock over
			// with it, which the host's transport never does. With MIDI clock they keep its grid, as the C++
			// arpeggiator keeps the host's
			if (g_ti)
				_cases.add(std::string(modes[mode]) + ", hold", s, keys, 34);
			_cases.add(std::string(modes[mode]) + ", hold, MIDI clock", s, keys, 34, true);
		}

		// Keys late for a step, with MIDI clock: up to half a step late plays at once. The firmware's clock lags MIDI
		// clock by a few milliseconds, up to a tenth of a step at times, a key closer to a step than that is early in
		// its eyes. Those are left out
		for (const int late : {25, 35, 45, 55, 75, 90})
		{
			auto s = _cases.base();
			s[g_arpPattern] = 2;
			const auto step = stepSamples(s);
			_cases.add(name("key %d%% late, MIDI clock", late), s, {{static_cast<uint64_t>(step * (4.0 + late / 100.0)), 60, 100}}, 12, true);
		}

		// Note Length, also with ratchets and triplets
		for (const int length : {0, 1, 16, 32, 63, 64, 65, 96, 127})
		{
			for (const int p : {1, 17, 38})
			{
				auto s = _cases.base();
				s[g_arpPattern] = static_cast<uint8_t>(p);
				s[g_arpNoteLength] = static_cast<uint8_t>(length);
				_cases.addChord(name("note length %d, pattern %02d", length, p), s, 40);
			}
		}

		// Swing
		for (const int swing : {1, 16, 32, 64, 100, 127})
		{
			for (const int p : {1, 17, 39})
			{
				auto s = _cases.base();
				s[g_arpPattern] = static_cast<uint8_t>(p);
				s[g_arpSwing] = static_cast<uint8_t>(swing);
				_cases.addChord(name("swing %d, pattern %02d", swing, p), s, 40);
			}
		}

		// Every Arp Clock. 1/128 (0 and 1) is a tick and a half of the firmware's arpeggiator: its steps take two ticks,
		// its notes come every four thirds of a step, and its first ones wherever its ticks fall, with MIDI clock too
		for (int clock = 0; clock < 18; ++clock)
		{
			auto s = _cases.base();
			s[g_arpClock] = static_cast<uint8_t>(clock);
			_cases.addChord(name("clock %d", clock), s, clock < 2 ? 120 : 40).steady = clock < 2;
		}

		// Clock Tempo, also with swing
		for (const int tempo : {0, 30, 90, 127})
		{
			for (const int swing : {0, 64})
			{
				auto s = _cases.base();
				s[g_clockTempo] = static_cast<uint8_t>(tempo);
				s[g_arpSwing] = static_cast<uint8_t>(swing);
				_cases.addChord(name("tempo %d BPM, swing %d", tempo + 63, swing), s, 40);
			}
		}
	}

	// A device that plays the arpeggio in C++ looks like one that lets the firmware play it: the same patches go in,
	// the same come out, and its firmware plays no arpeggio
	size_t checkTransparency(const std::vector<uint8_t>& _romData, const std::string& _romName, const ROMFile::TPreset& _base)
	{
		const auto model = g_ti ? DeviceModel::Snow : ROMLoader::detectModel(_romData);
		Firmware fw(_romData, _romName, model);
		Firmware cpp(_romData, _romName, model, true);

		size_t failed = 0;

		auto check = [&](const std::string& _name, const std::vector<std::pair<uint64_t, SMidiEvent>>& _events, const bool _plays)
		{
			// Every scenario holds a chord for a second, then lets go
			auto events = _events;
			const uint64_t t0 = 8820;
			for (const uint8_t n : {60, 64, 67})
			{
				events.emplace_back(t0, SMidiEvent(MidiEventSource::Host, M_NOTEON, n, 100));
				events.emplace_back(t0 + 44100, SMidiEvent(MidiEventSource::Host, M_NOTEOFF, n, 64));
			}

			auto notes = [](const std::vector<SMidiEvent>& _out)
			{
				size_t count = 0;
				for (const auto& e : _out)
					count += e.sysex.empty() && (e.a & 0xf0) == M_NOTEON && e.c > 0;
				return count;
			};

			const auto fwNotes = notes(fw.send(events, t0 + 44100 + 13230));
			const auto cppNotes = notes(cpp.send(events, t0 + 44100 + 13230));
			const auto fwSingle = fw.requestSingle();
			const auto cppSingle = cpp.requestSingle();

			std::string problems;
			if (cppNotes)
				problems += " the firmware played " + std::to_string(cppNotes) + " arpeggio notes despite the C++ arpeggiator;";
			if (_plays != (fwNotes > 0))
				problems += std::string(" the firmware arpeggiator ") + (_plays ? "did not play;" : "played;");
			if (fwSingle.empty() || fwSingle != cppSingle)
			{
				problems += " the edit buffer differs:";
				for (size_t i = 0; i < std::min(fwSingle.size(), cppSingle.size()); ++i)
				{
					if (fwSingle[i] != cppSingle[i])
						problems += " byte " + std::to_string(i) + " " + std::to_string(fwSingle[i]) + "/" + std::to_string(cppSingle[i]);
				}
			}

			std::printf("%s transparency: %s\n", problems.empty() ? "ok  " : "FAIL", _name.c_str());
			if (!problems.empty())
				std::printf("   %s\n", problems.c_str());
			std::fflush(stdout);
			failed += problems.empty() ? 0 : 1;
		};

		auto dump = [&](ROMFile::TPreset _single, const uint8_t _mode, const uint8_t _live)
		{
			_single[g_arpMode] = _mode;
			if (g_ti)
				_single[g_arpModeLive] = _live;
			SMidiEvent e(MidiEventSource::Host);
			e.sysex = Microcontroller::createSingleDump(fw.getRom(), BankNumber::EditBuffer, SINGLE, _single);
			return std::vector<std::pair<uint64_t, SMidiEvent>>{{0, e}};
		};

		auto param = [](const uint8_t _page, const uint8_t _index, const uint8_t _value)
		{
			SMidiEvent e(MidiEventSource::Host);
			e.sysex = {0xf0, 0x00, 0x20, 0x33, 0x01, 0x10, _page, 0x40, _index, _value, 0xf7};
			return std::vector<std::pair<uint64_t, SMidiEvent>>{{0, e}};
		};

		check("patch with Up, as factory patches keep it (page B 15 empty)", dump(_base, 1, 0), true);
		check("patch with Chord, both places", dump(_base, 6, 6), true);
		// The TI takes Arp Mode as parameter 15 of page B, the A, B and C as 1
		const uint8_t arpMode = g_ti ? 15 : 1;
		check("Arp Mode changed to Up&Down", param(0x71, arpMode, 3), true);
		check("Arp Mode changed to Off", param(0x71, arpMode, 0), false);
		check("patch without arpeggio", dump(_base, 0, 0), false);
		check("Arp Mode changed to As Played", param(0x71, arpMode, 4), true);
		if (g_ti)
			check("patch with Arp>Matrix", dump(_base, 7, 7), false);

		// A factory patch with an arpeggio, by program change
		for (int bank = 0; bank < 8; ++bank)
		{
			for (int program = 0; program < 128; ++program)
			{
				ROMFile::TPreset single{};
				if (!fw.getRom().getSingle(bank, program, single) || single[g_arpMode] < 1 || single[g_arpMode] > 6)
					continue;

				std::vector<std::pair<uint64_t, SMidiEvent>> events;
				// Bank A is 1, 0 is the edit buffer
				events.emplace_back(0, SMidiEvent(MidiEventSource::Host, M_CONTROLCHANGE, MC_BANKSELECTLSB, static_cast<uint8_t>(bank + 1)));
				events.emplace_back(0, SMidiEvent(MidiEventSource::Host, M_PROGRAMCHANGE, static_cast<uint8_t>(program)));
				check("program change to '" + ROMFile::getSingleName(single) + "' with Arp Mode " +
					std::to_string(single[g_arpMode]), events, true);
				return failed;
			}
		}

		std::puts("FAIL transparency: no factory patch with an arpeggio found");
		return failed + 1;
	}

	std::vector<uint8_t> readFile(const std::string& _path)
	{
		std::ifstream f(_path, std::ios::binary);
		return {std::istreambuf_iterator<char>(f), {}};
	}
}

int main(const int _argc, char* _argv[])
{
	baseLib::disableErrorDialogs();

	if (_argc < 2)
	{
		std::puts("usage: virusArpTest <rom file> [part of a case name]");
		return 2;
	}

	const std::string romName = _argv[1];
	const std::string filter = _argc > 2 ? _argv[2] : "";

	const auto romData = readFile(romName);

	// A TI firmware has megabytes, an A, B or C ROM half of one. A Snow runs the TI's firmware on one DSP, which is all
	// the arpeggiator needs
	g_ti = romData.size() > 1024 * 1024;
	const auto model = g_ti ? DeviceModel::Snow : ROMLoader::detectModel(romData);

	const ROMFile rom(romData, romName, model);
	if (!rom.isValid())
	{
		std::printf("%s is no Virus firmware\n", romName.c_str());
		return 2;
	}

	std::printf("Virus %s\n", rom.getModelName().c_str());
	if (rom.getModel() == DeviceModel::A)
		g_stepScale = 2.0;

	Firmware firmware(romData, romName, model);
	if (!firmware.isValid())
	{
		std::puts("the firmware did not start, or its arpeggiator patterns were not found");
		return 2;
	}

	ROMFile::TPreset base{};
	rom.getSingle(0, 0, base);

	Cases cases(base);
	createCases(cases, static_cast<int>(firmware.getArpeggiator().getPatternCount()));

	size_t failed = 0, run = 0;

	for (const auto& c : cases.get())
	{
		if (!filter.empty() && c.name.find(filter) == std::string::npos)
			continue;

		++run;

		// How far the firmware's clock is behind MIDI clock depends on what it played before, and one that followed MIDI
		// clock takes a while to run on its own again: a case with MIDI clock gets a firmware of its own. The TI's first
		// arpeggio after it started plays a late first note with the key's velocity, every later one with the step's:
		// one plays before
		std::unique_ptr<Firmware> fresh;
		if (c.midiClock)
		{
			fresh = std::make_unique<Firmware>(romData, romName, model);
			auto first = c;
			first.midiClock = false;
			first.keys = {{0, 60, 100}};
			first.length = static_cast<uint64_t>(4 * stepSamples(c.patch));
			fresh->play(first);
		}
		const auto& arp = firmware.getArpeggiator();
		const auto fw = sounding((fresh ? *fresh : firmware).play(c));

		double grid;
		std::vector<Note> cpp;

		if (c.steady)
		{
			cpp = playCpp(arp, c);
			const auto shift = findShift(c, fw, cpp);
			for (auto& n : cpp)
				n.time += shift;
			grid = std::fmod(static_cast<double>(shift), stepSamples(c.patch));
		}
		else
		{
			grid = findGrid(c, fw, playCpp(arp, c));
			cpp = playCpp(arp, c, &grid);
		}
		const auto result = compare(c, fw, cpp, grid);

		std::printf("%s %s\n%s", result.ok ? "ok  " : "FAIL", c.name.c_str(), result.text.c_str());
		std::fflush(stdout);

		failed += result.ok ? 0 : 1;
	}

	if (filter.empty() || filter == "transparency")
		failed += checkTransparency(romData, romName, cases.base());

	std::printf("%zu of %zu cases differ\n", failed, run);
	return failed ? 1 : 0;
}
