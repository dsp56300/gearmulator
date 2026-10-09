#include "arpeggiatorTI.h"

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

	bool ArpeggiatorTI::readPatterns(const dsp56k::Memory& _memory)
	{
		using namespace dsp56k;

		m_patterns.clear();

		// The firmware picks a pattern with
		//   move #>table,a / mac y1,#$10,a / move #>table+63,x0 / cmp x0,a
		// where the table holds the address of every pattern's steps. [0] is the patch's own pattern, it is not in there
		const auto sizeP = _memory.size(MemArea_P);

		for (TWord pc = 0; pc + 6 < sizeP; ++pc)
		{
			if (_memory.get(MemArea_P, pc) != 0x56f400 || _memory.get(MemArea_P, pc + 2) != 0x0110c2 ||
				_memory.get(MemArea_P, pc + 3) != 0x44f400 || _memory.get(MemArea_P, pc + 5) != 0x200045)
				continue;

			const auto table = _memory.get(MemArea_P, pc + 1);

			if (_memory.get(MemArea_P, pc + 4) != table + 63)
				continue;

			std::vector<std::vector<Event>> patterns(64);
			bool valid = true;

			for (TWord p = 1; p < patterns.size() && valid; ++p)
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

					steps.push_back({static_cast<float>(length) / 65536.0f, static_cast<float>(duration) / 65536.0f,
						static_cast<uint8_t>(velocity)});

					total += duration;
					address += 3;
				}

				valid = !steps.empty() && steps.size() < g_maxPatternSteps && total > 0;
			}

			if (!valid)
				continue;

			m_patterns = std::move(patterns);
			return true;
		}

		return false;
	}

	void ArpeggiatorTI::process(const std::array<const ROMFile::TPreset*, 16>& _singles,
		std::vector<synthLib::SMidiEvent>& _midiIn, std::vector<synthLib::SMidiEvent>& _out, const uint32_t _sampleCount,
		const float _samplerate, const synthLib::HostTransport& _transport)
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

			m_arps[channel].process(_midiIn, _out, _sampleCount, _samplerate, _transport);
		}
	}

	ArpeggiatorTI::Key ArpeggiatorTI::createKey(const ROMFile::TPreset* _single)
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

	synthLib::ArpSettings ArpeggiatorTI::createSettings(const uint8_t _channel, const Key& _key) const
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
		settings.division = {g_clockDividers[std::min<size_t>(_key[7], std::size(g_clockDividers) - 1)], 384};
		settings.swing = 0.5f + static_cast<float>(_key[5]) / 512.0f;

		// The firmware scales every note by Arp Note Length, 64 being 1, and cuts it where the next note starts
		const auto noteLength = static_cast<float>(_key[4]) / 64.0f;

		auto addStep = [&](const bool _active, const float _length, const float _duration, const float _maxLength,
			const uint8_t _velocity)
		{
			ArpStep step;
			step.active = _active;
			step.duration = _duration;
			step.length = std::min(_length * noteLength, _maxLength);
			// The key's velocity, moved by the step's, 100 being no change
			step.velocityOffset = static_cast<int8_t>(static_cast<int>(_velocity) - 100);
			settings.pattern.push_back(step);
		};

		if (const auto pattern = _key[1])
		{
			for (const auto& e : m_patterns[std::min<size_t>(pattern, m_patterns.size() - 1)])
				addStep(true, e.length, e.duration, e.duration, e.velocity);
			return settings;
		}

		// The patch's own pattern. A rest lengthens the note before it, its length is a share of the whole gap
		const auto count = std::min<int>(_key[8], 31) + 1;
		auto field = [&](const int _step, const int _field) { return _key[9 + (_step % count) * 3 + _field]; };

		for (int i = 0; i < count; ++i)
		{
			int gap = 1;
			while (gap < count && !(field(i + gap, 2) & 1))
				++gap;

			addStep(field(i, 2) & 1, static_cast<float>(field(i, 0)) / 128.0f * static_cast<float>(gap), 1.0f,
				static_cast<float>(gap), field(i, 1));
		}

		return settings;
	}
}
