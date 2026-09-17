#include "88lib/boards/cm32p.h"

#include <algorithm>
#include <cmath>

#include "common/romDescramble.h"

namespace emu88Lib
{
	namespace
	{
		// The board wave-ROM order is rLib::rom::Wave19.
		// MAME's card software list is in that order too; other card dumps in
		// circulation have address lines 7-16 permuted differently and are
		// rLib::rom::Wave19Card, with the same data lines.
		using DecodeChip = void (*)(const uint8_t*, uint8_t*);

		// One 512 KiB chip image, raw to decoded.
		template<typename Perm> void decodeChip(const uint8_t* raw, uint8_t* decoded)
		{
			Perm::descramble(raw, Cm32pRomSet::WaveSize, decoded, Cm32pRomSet::WaveSize);
		}

		// The VCA's control voltage is the CPU's PWM smoothed by R63 82k into C89 0.1uF, the
		// same network the CM-32L uses on its own.
		constexpr float g_vcaTimeConstant = 82e3f * 0.1e-6f;

		// How many of the first eight tone names' characters read as a name. Across 19 cards the
		// right dump order scores 75-80 of 80 and the wrong one 36 at most.
		unsigned toneNameScore(const std::vector<uint8_t>& card)
		{
			unsigned score = 0;
			for(uint32_t entry = 0; entry < 8; ++entry)
				for(uint32_t i = 0; i < 10; ++i)
				{
					const auto c = card[0x1000 + entry * 0x50 + i];
					score += (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
					         c == ' ' || c == '.' || c == '\'';
				}
			return score;
		}
	}

	std::vector<uint8_t> Cm32p::decodeWaves(const WaveRoms& waves)
	{
		std::vector<uint8_t> decoded(0x400000, 0xff);
		for(size_t bank = 0; bank < waves.size(); ++bank)
			decodeChip<rLib::rom::Wave19>(waves[bank].data(), decoded.data() + bank * 0x100000);
		return decoded;
	}

	std::vector<uint8_t> Cm32p::decodeCard(const std::vector<uint8_t>& image)
	{
		if(image.empty() || image.size() > CardSize) return {};
		// A smaller card repeats through the window, as in MAME: in steps of the largest power of
		// two that fits, but at least 128 KiB, the span the address scramble keeps together.
		std::vector<uint8_t> raw(CardSize, 0xff);
		std::copy(image.begin(), image.end(), raw.begin());
		uint32_t mirror = 0x20000;
		while(mirror * 2 <= image.size()) mirror *= 2;
		for(uint32_t offset = mirror; offset < CardSize; offset += mirror)
			std::copy_n(raw.begin(), mirror, raw.begin() + offset);

		std::vector<uint8_t> best;
		unsigned bestScore = 0;
		for(const DecodeChip decode : {decodeChip<rLib::rom::Wave19>, decodeChip<rLib::rom::Wave19Card>})
		{
			std::vector<uint8_t> card(CardSize);
			decode(raw.data(), card.data());
			if(const auto score = toneNameScore(card); score > bestScore)
			{
				bestScore = score;
				best = std::move(card);
			}
		}
		// At most two unreadable characters per name, and far above what the wrong order scores.
		return bestScore >= 64 ? best : std::vector<uint8_t>{};
	}

	Cm32p::Cm32p(const Cm32pRomSet& roms, const std::vector<uint8_t>& card)
		: m_waves(roms.isValid() ? decodeWaves(roms.waves) : std::vector<uint8_t>{}), m_lp(m_waves)
	{
		m_valid = !m_waves.empty();
		if(!m_valid) return;
		if(const auto decodedCard = decodeCard(card); !decodedCard.empty())
		{
			std::copy(decodedCard.begin(), decodedCard.end(), m_waves.begin() + CardBase);
			m_cardInserted = true;
		}
		auto& bus = m_machine.bus();
		bus.load(0, roms.program.data(), roms.program.size());
		bus.map_rom(0x2000, 0x100);
		bus.map_ram(0x2100, 0x1f00);
		bus.map_rom(0x4000, 0xc000);
		bus.map_device(0x1080, 0x80, &m_host);
		bus.map_device(0x1100, 0x80, &m_host);
		bus.map_device(0x1300, 0x80, &m_host);
		bus.map_device(0x1400, 0x100, &m_host);
		m_lp.setInterruptCallback([this](bool level) { m_machine.periph().set_external_interrupt_input(0, level); });
		m_machine.periph().set_serial_tx_byte_hook([this](uint8_t value) { m_midiOut.write(value); });
		reset();
	}

	void Cm32p::reset()
	{
		if(!m_valid) return;
		m_machine.sched().cancel(m_lcdReadyEvent);
		m_lcdReadyEvent = 0;
		std::fill_n(m_machine.bus().ptr(0x2100), 0x1f00, 0);
		m_machine.cpu().invalidate_range(0x2100, 0x1f00);
		m_lp.reset();
		m_rcc.reset();
		// C89 starts discharged, so the board fades up as the firmware takes the PWM down.
		m_vcaGain = 0.0f;
		m_serviceLcd.reset();
		m_midiOut = synthLib::MidiBufferParser(synthLib::MidiEventSource::Device);
		m_midiIn = std::make_unique<synthLib::MidiRateLimiter>([this](uint8_t value) { m_machine.periph().receive_serial(value); });
		m_midiIn->setSamplerate(SampleRate);
		m_midiIn->setRateLimit(3125);
		m_midiIn->setPreserveEventOrder(true);
		m_machine.reset();
		// Service switches released. The card-detect line is P0.4, the pin that is also analog
		// input ACH4, and it is high with a card in the slot.
		m_machine.periph().set_port_input(0, m_cardInserted ? 0xd0 : 0xc0);
		m_machine.periph().set_analog_input(4, m_cardInserted ? 0x3ff : 0);
		m_machine.periph().set_port_input(1, 0xff);
		m_machine.periph().set_port_input(2, 0xff);
		m_machine.periph().set_analog_input(7, 0);
		m_cycleTarget = m_machine.now();
	}

	uint8_t Cm32p::leds() const
	{
		// MIDI MESSAGE is active low: HSO.3 idles high and the firmware pulls it down to light it.
		return m_valid && (m_machine.periph().hso_output() & 0x08) == 0 ? 1 : 0;
	}

	uint8_t Cm32p::Host::read8(uint32_t address)
	{
		if(address >= 0x1080 && address <= 0x10ff) return board.m_rcc.read(static_cast<uint16_t>(address));
		if(address == 0x1100) return board.m_lcdReadyEvent ? 1 : 0;
		if(address >= 0x1400 && address < 0x1420) return board.m_lp.read(static_cast<uint8_t>(address));
		return 0xff;
	}

	void Cm32p::Host::write8(uint32_t address, uint8_t value)
	{
		if(address >= 0x1080 && address <= 0x10ff) board.m_rcc.write(static_cast<uint16_t>(address), value);
		else if(address == 0x1100 || address == 0x1102) board.writeLcd(address == 0x1102, value);
		else if(address >= 0x1400 && address < 0x1420) board.m_lp.write(static_cast<uint8_t>(address), value);
	}

	void Cm32p::writeLcd(bool data, uint8_t value)
	{
		m_serviceLcd.write(data, value);
		auto& scheduler = m_machine.sched();
		scheduler.cancel(m_lcdReadyEvent);
		m_machine.periph().set_hsi_input(0, false);
		// IC8's LCD INT feeds HSI0. The firmware starts its queue in software,
		// then needs a completion interrupt after each LCD write to drain it.
		// Approximate controller execution times; the gate-array delay is unmeasured.
		const auto microseconds = !data && (value == 1 || (value & 0xfe) == 2) ? 1640u : 40u;
		m_lcdReadyEvent = scheduler.schedule(m_machine.now() + CpuStateRate / 1000000 * microseconds,
			[](void* context, uint64_t, uint64_t)
			{
				auto& board = *static_cast<Cm32p*>(context);
				board.m_lcdReadyEvent = 0;
				board.m_machine.periph().set_hsi_input(0, true);
			}, this);
	}

	float Cm32p::applyVca()
	{
		// The PWM's duty is the attenuation, as on the CM-32L. This firmware parks it at 0 and
		// only sweeps it while the board powers on, so in normal use the VCA is transparent and
		// this is the mute ramp; nothing else has been seen to move it.
		const auto duty = static_cast<float>(m_machine.periph().pwm_duty());
		const auto target = 1.0f - duty * (1.0f / 255.0f);
		const auto alpha = 1.0f - std::exp(-1.0f / (static_cast<float>(SampleRate) * g_vcaTimeConstant));
		m_vcaGain += (target - m_vcaGain) * alpha;
		return m_vcaGain;
	}

	Cm32p::SampleFrame Cm32p::renderSample()
	{
		if(!m_valid) return {};
		m_midiIn->processSample();
		m_cycleTarget += CpuStateRate / SampleRate;
		if(m_machine.now() < m_cycleTarget) m_machine.run(m_cycleTarget - m_machine.now());
		const auto output = m_rcc.processFrame(m_lp.renderSample());
		if(!output) return {};
		// Slot 2 carries left and slot 0 right: the firmware's test mode plays "PCM OUT L" on
		// slot 2, and MIDI pan then follows the MT-32's convention (CC10 = 0 is right). Scale the
		// signed 16-bit DAC words to the shared board interface's 24-bit full scale.
		const auto gain = applyVca();
		const auto scale = [gain](const int32_t _word)
		{
			return static_cast<int32_t>(static_cast<float>(_word) * gain) * 256;
		};
		return {scale(output->serialLoadWords[2]), scale(output->serialLoadWords[0])};
	}

	void Cm32p::addMidiEvent(const synthLib::SMidiEvent& event, uint8_t port)
	{
		if(m_midiIn && port == 0) m_midiIn->write(synthLib::SMidiEvent(event));
	}

	void Cm32p::readMidiOut(std::vector<synthLib::SMidiEvent>& events)
	{
		m_midiOut.getEvents(events);
	}

	void Cm32p::transportDiscontinuity(uint32_t generation)
	{
		if(m_midiIn) m_midiIn->transportDiscontinuity(generation);
	}
}
