#include "arpeggiator.h"

#include <algorithm>

#include "dsp56kEmu/memory.h"

namespace virusLib
{
	namespace
	{
		// Arp Clock, in 1/384 of a whole note: 1/128, 1/64, 1/32, 1/16, 1/8, 1/4, 3/128, 3/64, 3/32, 3/16, 1/48, 1/24,
		// 1/12, 1/6, 1/3, 3/8, 1/2. The firmware treats 0 like 1
		constexpr uint16_t g_clockDividers[] = {3, 3, 6, 12, 24, 48, 96, 9, 18, 36, 72, 8, 16, 32, 64, 128, 144, 192};

		constexpr uint32_t g_maxPatternSteps = 512;
	}

	bool Arpeggiator::readPatterns(const dsp56k::Memory& _memory, const DeviceModel _model)
	{
		using namespace dsp56k;

		m_patterns.clear();

		// The firmware picks a pattern with
		//   move #>table,a / mac y1,#$10,a / move #>last,x0 / cmp x0,a
		// where the table holds the address of every pattern's steps: 64 on the C and the TI, 40 before. On the TI, [0]
		// is the patch's own pattern, which the firmware takes from the patch instead
		const auto sizeP = _memory.size(MemArea_P);

		for (TWord pc = 0; pc + 6 < sizeP; ++pc)
		{
			if (_memory.get(MemArea_P, pc) != 0x56f400 || _memory.get(MemArea_P, pc + 2) != 0x0110c2 ||
				_memory.get(MemArea_P, pc + 3) != 0x44f400 || _memory.get(MemArea_P, pc + 5) != 0x200045)
				continue;

			const auto table = _memory.get(MemArea_P, pc + 1);
			const auto last = _memory.get(MemArea_P, pc + 4);

			if (last <= table || last - table >= 128)
				continue;

			std::vector<std::vector<Event>> patterns(last - table + 1);
			bool valid = true;

			for (TWord p = isTIFamily(_model) ? 1 : 0; p < patterns.size() && valid; ++p)
			{
				// Three words per step, in 8.16 fixed point: the note length, the distance to the next step and the
				// velocity. $ff0000 ends the pattern
				auto address = _memory.get(MemArea_X, table + p);
				auto& steps = patterns[p];
				double total = 0;

				while (steps.size() < g_maxPatternSteps)
				{
					const auto length = _memory.get(MemArea_X, address);
					if (length == 0xff0000)
						break;

					const auto duration = _memory.get(MemArea_X, address + 1);
					const auto velocity = std::min<TWord>(_memory.get(MemArea_X, address + 2) >> 16, 127);

					steps.push_back({length, duration, static_cast<uint8_t>(velocity)});

					total += duration;
					address += 3;
				}

				valid = !steps.empty() && steps.size() < g_maxPatternSteps && total > 0;
			}

			if (!valid)
				continue;

			m_patterns = std::move(patterns);
			m_model = _model;
			return true;
		}

		return false;
	}

	void Arpeggiator::process(const std::array<const ROMFile::TPreset*, 16>& _singles,
		std::vector<synthLib::SMidiEvent>& _midi, std::vector<synthLib::SMidiEvent>& _out, const uint32_t _sampleCount,
		const float _samplerate)
	{
		for (uint8_t channel = 0; channel < m_arps.size(); ++channel)
		{
			const auto key = createKey(_singles[channel]);

			if (!m_keyValid[channel] || key != m_keys[channel])
			{
				m_keys[channel] = key;
				m_keyValid[channel] = true;
				m_arps[channel].setSettings(createSettings(channel, key));
			}

			m_arps[channel].process(_midi, _out, _sampleCount, _samplerate);
		}
	}

	Arpeggiator::Key Arpeggiator::createKey(const ROMFile::TPreset* _single)
	{
		Key key{};

		if (!_single)
			return key;

		const auto& s = *_single;

		// Page B: Arp Mode as the patch keeps it (1), Pattern, Octave Range, Hold, Note Length, Swing (2-6), Clock Tempo
		// and Arp Clock (16, 17). Page 6E: the length of the patch's own pattern (127), page 6F its 32 steps
		std::copy_n(s.begin() + 128 + 1, 6, key.begin());
		key[6] = s[128 + 16];
		key[7] = s[128 + 17];
		key[8] = s[256 + 127];
		std::copy_n(s.begin() + 384, 96, key.begin() + 9);

		return key;
	}

	synthLib::ArpSettings Arpeggiator::createSettings(const uint8_t _channel, const Key& _key) const
	{
		using namespace synthLib;

		ArpSettings settings;
		settings.channel = _channel;

		// Off, Up, Down, Up&Down, As Played, Random and Chord are in the order of ArpMode. Arp>Matrix (7) modulates
		// instead of playing notes, which this cannot do
		const auto mode = _key[0];
		if (mode == 0 || mode > 6)
			return settings;

		settings.mode = static_cast<ArpMode>(mode);
		settings.octaves = static_cast<uint8_t>((_key[2] & 3) + 1);
		settings.hold = _key[3] != 0;
		settings.tempo = static_cast<float>(_key[6] + 63);

		// The firmware takes Arp Clock 0 as 1. The A counts a tick of 1/192 note for every unit of it, where the others
		// count one for every two: 1/16 plays eighths there. Its DSP rounds halves up, the others' to even
		const bool a = m_model == DeviceModel::A;
		const auto units = g_clockDividers[std::clamp<size_t>(_key[7], 1, std::size(g_clockDividers) - 1)];
		const auto clock = static_cast<uint16_t>(a ? units * 2 : units);
		settings.division = {clock, 384};

		// With MIDI clock, a key struck up to half a step late still plays at once, on the A, B and C the key itself.
		// The A starts on the grid of the others' steps, half of its own. As Played starts with the second note. The
		// A, B and C start over when hold gets a new set of keys, the TI goes on. See virusArpTest
		const bool ti = isTIFamily(m_model);
		settings.startGrid = a ? 0.5f : 1.0f;
		settings.lateStart = 0.5f;
		settings.lateStartPlaysKey = !ti;
		settings.asPlayedFromSecond = true;
		settings.holdRestarts = !ti;

		// The A has no Pattern, Note Length and Swing in its patches, it plays pattern 1 as it is
		if (a)
		{
			compile(m_patterns[1], clock, 64, 0, false, settings.pattern);
			return settings;
		}

		if (const auto pattern = _key[1]; pattern || !ti)
		{
			compile(m_patterns[std::min<size_t>(pattern, m_patterns.size() - 1)], clock, _key[4], _key[5], true,
				settings.pattern);
			return settings;
		}

		// The patch's own pattern, converted as the firmware does: a step that plays lasts until the next one that
		// does, and its length is a share of all of that. Rests before the first one that plays are a step of their
		// own, a silent one. A step that plays has a velocity of at least 1
		const auto count = std::min<uint32_t>(_key[8], 31) + 1;
		auto field = [&](const uint32_t _step, const uint32_t _field) { return _key[9 + _step * 3 + _field]; };

		std::vector<Event> events;
		for (uint32_t i = 0; i < count; ++i)
		{
			const bool plays = field(i, 2) & 1;
			if (!plays && i)
				continue;

			uint32_t gap = 1;
			while (i + gap < count && !(field(i + gap, 2) & 1))
				++gap;

			events.push_back({field(i, 0) * gap * 512u, gap << 16, static_cast<uint8_t>(plays ? std::max<uint8_t>(field(i, 1), 1) : 0)});
		}

		compile(events, clock, _key[4], _key[5], true, settings.pattern);

		return settings;
	}

	void Arpeggiator::compile(const std::vector<Event>& _events, const uint32_t _clockUnits, const uint32_t _noteLength,
		const uint32_t _swing, const bool _roundToEven, std::vector<synthLib::ArpStep>& _steps)
	{
		// The firmware turns a pattern into ticks of 1/192 note before it plays it, every step on its own: its
		// duration with swing, and its note length, cut where the step ends. Rounded where the firmware rounds, in
		// its fixed point, see $05186a in OS 5.1.7. A pattern whose steps do not end on ticks loses time with that

		// mpyr: the product of two fractions, rounded to 24 bits
		auto mpyr = [_roundToEven](const int64_t _a, const int64_t _b)
		{
			const auto p = _a * _b;
			const auto q = p >> 23;
			const auto r = p & 0x7fffff;
			return r > 0x400000 || (r == 0x400000 && (!_roundToEven || (q & 1))) ? q + 1 : q;
		};

		// Swing: the share of a pair of steps that the first one takes, 0.5 + swing / 512, as fractions
		const int64_t first = (1ll << 22) + static_cast<int64_t>(_swing) * (1ll << 14);
		const int64_t second = (1ll << 23) - first;

		const double ticksPerStep = _clockUnits / 2.0;

		int64_t pos = 0;	// from the start of the pattern, in steps, 8.16

		for (const auto& e : _events)
		{
			auto gate = mpyr(e.length, static_cast<int64_t>(_clockUnits) * _noteLength);
			if (!gate)
				gate = 1;

			// Whole pairs of steps take as long with swing as without. Of what remains, every part takes the share of
			// the step it falls into: what is left of the step at the position, unless the remainder ends before
			// that, the step after it, and the one after that
			const int64_t pairs = e.duration & 0xfe0000;
			const int64_t rest = e.duration - pairs;
			pos += pairs;

			const int64_t left = (pos & 0xffff) ? 0x10000 - (pos & 0xffff) : 0;
			const int64_t inStep = left > rest ? 0 : left;
			pos += inStep;

			const bool odd = (pos >> 16) & 1;
			const int64_t inNext = std::min<int64_t>(rest - inStep, 0x10000);
			const int64_t beyond = rest - inStep - inNext;
			pos += rest - inStep;

			const auto next = odd ? second : first;
			const auto other = odd ? first : second;
			const auto swung = pairs + ((inStep * other + inNext * next + beyond * other) >> 22);

			const auto ticks = mpyr(swung, static_cast<int64_t>(_clockUnits) * 64);
			gate = std::min(gate, ticks);

			synthLib::ArpStep step;
			step.duration = static_cast<float>(static_cast<double>(ticks) / ticksPerStep);
			step.length = static_cast<float>(static_cast<double>(gate) / ticksPerStep);
			// The key's velocity, moved by the step's, 100 being no change. A step with none is silent
			step.active = e.velocity != 0;
			step.velocityOffset = static_cast<int8_t>(static_cast<int>(e.velocity) - 100);
			_steps.push_back(step);
		}
	}
}
