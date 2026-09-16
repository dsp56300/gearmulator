#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "cpu/sh2/machine.hpp"
#include "custom_chips/lsp/lsp.h"
#include "custom_chips/xp/xp.h"
#include "synthLib/midiBufferParser.h"
#include "88lib/mcu/sc8850_submcu.h"
#include "synthLib/midiRateLimiter.h"

namespace emu88Lib
{
	// SC-8820 mainboard. The currently available CPU image is reconstructed,
	// not an original dump. The USB interface exposes part groups A and B.
	class Sc8820
	{
	public:
		using SampleFrame = std::pair<int32_t, int32_t>;
		static constexpr uint32_t CpuRomSize = 0x20000;
		static constexpr uint32_t ReconstructedCpuRomSize = 0x10000;
		static constexpr uint32_t ProgramRomSize = 0x200000;
		static constexpr uint32_t SampleRate = 32000;
		static constexpr uint32_t CpuClockHz = 28224000;

		enum class Button : uint8_t { InstMap, VolumePush };

		Sc8820(const std::vector<uint8_t>& cpuRom, const std::vector<uint8_t>& programRom,
			std::vector<uint8_t> wave0, std::vector<uint8_t> wave1);
		bool isValid() const { return m_valid; }
		void reset();
		SampleFrame renderSample();
		void addMidiEvent(const synthLib::SMidiEvent& event, uint8_t port = 0);
		void readMidiOut(std::vector<synthLib::SMidiEvent>& events);
		void transportDiscontinuity(uint32_t generation);
		void setButton(Button button, bool pressed);
		uint16_t leds() const;

	private:
		struct XpHost final : emu::Device
		{
			explicit XpHost(xpLib::XP& xp) : chip(xp) {}
			uint8_t read8(uint32_t a) override { return chip.hostRead8(uint16_t(a)); }
			void write8(uint32_t a, uint8_t v) override { chip.hostWrite8(uint16_t(a), v); }
			uint16_t read16(uint32_t a) override { return chip.hostRead(uint16_t(a)); }
			void write16(uint32_t a, uint16_t v) override { chip.hostWrite(uint16_t(a), v); }
			xpLib::XP& chip;
		};
		struct LspHost final : emu::Device
		{
			explicit LspHost(lspLib::LSPDispatcher& lsp) : chip(lsp) {}
			uint8_t read8(uint32_t a) override { return chip.hostRead(uint16_t(a & 15)); }
			void write8(uint32_t a, uint8_t v) override { chip.hostWrite(uint16_t(a & 15), v); }
			lspLib::LSPDispatcher& chip;
		};
		struct ControllerHost final : emu::Device
		{
			explicit ControllerHost(Sc8850SubMcu& controller) : chip(controller) {}
			uint8_t read8(uint32_t address) override { return chip.hostRead(address); }
			void write8(uint32_t address, uint8_t value) override { chip.hostWrite(address, value); }
			Sc8850SubMcu& chip;
		};

		void updateLamps();

		sh2::Machine m_machine{sh2::ChipModel::SH7017, 2};
		std::array<std::vector<uint8_t>, 2> m_waves;
		xpLib::XP m_xp;
		lspLib::LSPDispatcher m_lsp;
		XpHost m_xpHost{m_xp};
		LspHost m_lspHost{m_lsp};
		Sc8850SubMcu m_usb{Sc8850SubMcu::BootProtocol::Sc8820};
		ControllerHost m_controller{m_usb};
		std::array<std::unique_ptr<synthLib::MidiRateLimiter>, 2> m_midiIn;
		synthLib::MidiBufferParser m_midiOut{synthLib::MidiEventSource::Device};
		std::array<int32_t, 2> m_lspReturn{};
		std::array<uint8_t, 3> m_lampRows{};
		uint64_t m_cycleTarget = 0;
		uint16_t m_buttonPins = 0xffff;
		bool m_valid = false;
	};
}
