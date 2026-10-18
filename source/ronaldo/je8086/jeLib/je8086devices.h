#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "common/device.hpp"
#include "common/sched.hpp"
#include "cpu/h8300h/adc.hpp"
#include "cpu/h8300h/machine.hpp"

#include "custom_chips/esp/esp.h"

namespace jeLib
{
	namespace devices
	{
		enum SwitchType : uint8_t	// bits: [LSC-EN1][LSC-EN0][LSC2-0][3 bits]
		{
			kSwitch_Sync = 64,
			kSwitch_Osc2Waveform,
			kSwitch_PanLfo,
			kSwitch_Exit,
			kSwitch_PerformSel = 72,
			kSwitch_3,
			kSwitch_6,
			kSwitch_Write,
			kSwitch_ValueUp = 80,
			kSwitch_2,
			kSwitch_5,
			kSwitch_8,
			kSwitch_TVF24,
			kSwitch_ValueDown = 88,
			kSwitch_1,
			kSwitch_4,
			kSwitch_7,
			kSwitch_TVFType,
			/* the above repeats for 96->127*/
			kSwitch_Upper = 128,
			kSwitch_KeyMode,
			kSwitch_Rec,
			kSwitch_Hold,
			kSwitch_Range,
			kSwitch_OnOff,
			kSwitch_Mode,
			kSwitch_Lower = 136,
			kSwitch_Motion2,
			kSwitch_Motion1,
			kSwitch_Osc1Waveform,
			kSwitch_LFO1Destination,
			kSwitch_Ring,
			kSwitch_LFO1Waveform,
			kSwitch_Portamento = 160,
			kSwitch_OctaveUp,
			kSwitch_RibbonAssign,
			kSwitch_Mono = 168,
			kSwitch_OctaveDown,
			kSwitch_VelocityAssign,
			kSwitch_BendRange = 176,
			kSwitch_Relative,
			kSwitch_VelocityOnOff,
			kSwitch_Hold2 = 185,
			kSwitch_DepthSelect
		};

		// A/D channel = [3: AN line][3: port 6 mux select]; VR n is channel 15 + n
		enum FaderType : uint8_t
		{
			kFader_PitchBend = 0,
			kFader_ModWheel = 8,
			kFader_Expression,
			kFader_BattSense = 11,
			kFader_Osc2Range = 16,
			kFader_Osc2PwmDepth,
			kFader_TvfResonance,
			kFader_Osc2PulseWidth,
			kFader_Bass,
			kFader_TvfFreq,
			kFader_Treble,
			kFader_TvfEnvA,
			kFader_Chorus = 24,
			kFader_TvfEnvS,
			kFader_TvfEnvR,
			kFader_TvfEnvD,
			kFader_DelayTime,
			kFader_TvaEnvA,
			kFader_DelayFb,
			kFader_DelayLevel,
			kFader_TvfEnvDepth = 32,
			kFader_TvfLFO1,
			kFader_TvfKeyFollow,
			kFader_TvaLevel,
			kFader_TvaEnvD,
			kFader_TvaLFO1Depth,
			kFader_TvaEnvS,
			kFader_TvaEnvR,
			kFader_Osc1Ctrl2 = 40,
			kFader_Osc1Ctrl1 = 44,
			kFader_FineTune = 46,
			kFader_Tempo = 48,
			kFader_PortaTime,
			kFader_LFO2Depth,
			kFader_LFO2Rate,
			kFader_Ribbon1,
			kFader_Ribbon2,
			kFader_Unused1,
			kFader_Unused2,
			kFader_LFO1Rate = 56,
			kFader_LFO1Fade,
			kFader_OscBal,
			kFader_XModDepth,
			kFader_LFO1Depth,
			kFader_PitchEnvDepth,
			kFader_PitchEnvA,
			kFader_PitchEnvD
		};

		// Words copied from one ESP's output GRAM (0x80 + k) into the next one's input GRAM (k) after every frame.
		struct GramRoute
		{
			uint8_t src, dst, first, last;
		};

		// The ESP ASICs, 0x4000 bytes of host window each, all clocked by the same word clock. Every frame each chip
		// runs, the last one produces the sample, then the GRAM routes hand data down the chain.
		template<int... EramBits>
		class MultiAsic final : public emu::Device
		{
		public:
			static constexpr size_t Count = sizeof...(EramBits);
			static constexpr uint32_t WindowSize = 0x4000;
			static constexpr uint32_t StepsPerFrame = 384;

			explicit MultiAsic(std::vector<GramRoute> _routes) : m_routes(std::move(_routes))
			{
				forEach([](auto& _asic)
				{
					_asic.setDintFrameCycles(StepsPerFrame);
					_asic.setExternalClockPresent(true);
				});
			}

			template<typename F> void forEach(F&& _func)
			{
				std::apply([&](auto&... _asics) { (_func(_asics), ...); }, m_asics);
			}
			template<typename F> void forEach(F&& _func) const
			{
				std::apply([&](const auto&... _asics) { (_func(_asics), ...); }, m_asics);
			}

			template<typename F> void visit(const size_t _index, F&& _func)
			{
				size_t i = 0;
				forEach([&](auto& _asic) { if (i++ == _index) _func(_asic); });
			}

			void setEspEngine(const esp::Engine _engine) { forEach([_engine](auto& _asic) { _asic.setEngine(_engine); }); }

			auto& first() { return std::get<0>(m_asics); }
			const auto& first() const { return std::get<0>(m_asics); }
			const auto& last() const { return std::get<Count - 1>(m_asics); }

			// one bit per chip: its DINT pin
			uint8_t readDintLines() const
			{
				uint8_t result = 0;
				size_t i = 0;
				forEach([&](const auto& _asic) { result |= static_cast<uint8_t>((_asic.readDint() & 1) << i++); });
				return result;
			}

			uint8_t read8(const uint32_t _address) override
			{
				uint8_t result = 0;
				visit(index(_address), [&](auto& _asic) { result = _asic.hostRead(_address & (WindowSize - 1)); });
				return result;
			}

			void write8(const uint32_t _address, const uint8_t _value) override
			{
				visit(index(_address), [&](auto& _asic) { _asic.hostWrite(_address & (WindowSize - 1), _value); });
			}

			// _onFrame(left, right) per produced frame; returns whether any frame ran
			template<typename F> bool runForCycles(const uint64_t _cycles, F&& _onFrame)
			{
				const uint64_t diff = _cycles + m_cyclesResidual - m_lastCycles;
				m_lastCycles = _cycles;

				const uint64_t frames = diff / StepsPerFrame;
				m_cyclesResidual = diff % StepsPerFrame;

				for (uint64_t f = 0; f < frames; ++f)
				{
					forEach([](auto& _asic) { _asic.runProgram(StepsPerFrame); });

					_onFrame(last().readGRAM(0xe8), last().readGRAM(0xec));

					for (const auto& r : m_routes)
					{
						visit(r.src, [&](auto& _src)
						{
							visit(r.dst, [&](auto& _dst)
							{
								for (int k = r.first; k <= r.last; k += 2)
									_dst.writeGRAM(_src.readGRAM(static_cast<uint8_t>(0x80 + k)), static_cast<uint8_t>(k));
							});
						});
					}

					forEach([](auto& _asic) { _asic.sync(); });
				}
				return frames != 0;
			}

		private:
			static size_t index(const uint32_t _address)
			{
				return std::min<size_t>((_address >> 14) & 7, Count - 1);
			}

			std::tuple<esp::Dispatcher<EramBits>...> m_asics;
			std::vector<GramRoute> m_routes;
			uint64_t m_lastCycles = 0;
			uint64_t m_cyclesResidual = 0;
		};

		// ASIC3 needs 2^19 ERAM words although the board has 2^18: the ESP's high-offset address adjust (0x40000 /
		// 0xc0000) only survives a 2^19 mask, at 2^18 those taps wrap to zero.
		using KeyboardAsics = MultiAsic<17, 0, 0, 19>;
		using RackAsics = MultiAsic<19, 19, 19, 19, 19>;

		// Port A / port B: the switch matrix and LED latches, plus the TPC outputs the ITU drives onto the same pins
		class Port final : public emu::Device
		{
		public:
			explicit Port(uint32_t _rowCount);

			void reset();
			void releaseAll();
			void press(uint32_t _which, bool _down = true);
			void setTpcOutput(uint16_t _value, uint16_t _mask);

			uint8_t read8(uint32_t _address) override;
			void write8(uint32_t _address, uint8_t _value) override;

		private:
			const uint32_t m_rowCount;
			std::array<int8_t, 32> m_switches{};
			std::array<int8_t, 32> m_leds{};
			int8_t m_portA = 0;
			int8_t m_portBDdr = 0;
			int8_t m_portB = 0;
			bool m_latchArmed = false;
		};

		// The multiplexed potentiometers behind the H8/3002 A/D converter: port 6 selects the mux input, the converter
		// (ADCSR / ADCR / ADDRA-D) reads them
		class Faders final : public emu::Device
		{
		public:
			using ChannelMap = std::array<uint8_t, 64>;

			Faders(h8300h::Machine& _machine, const ChannelMap& _channels);

			void reset();
			uint8_t read8(uint32_t _address) override;
			void write8(uint32_t _address, uint8_t _value) override;
			void setFader(int _which, int _value);	// value 0..1023

			static ChannelMap keyboardChannels();

		private:
			const ChannelMap m_channels;
			int8_t m_port6 = 0;
			std::array<int, 64> m_values{};
			h8300h::Adc m_adc;
		};

		class KeyScanner final : public emu::Device
		{
		public:
			uint8_t read8(uint32_t) override { return 0; }
			void write8(uint32_t, uint8_t) override {}
		};

		// One bus line shared by several small devices, bytes nobody claims go to the fallback
		class LineMux final : public emu::Device
		{
		public:
			explicit LineMux(emu::Device* _fallback) : m_fallback(_fallback) {}
			void assign(const uint32_t _address, const uint32_t _length, emu::Device* _device)
			{
				m_entries.push_back({_address, _length, _device});
			}
			uint8_t read8(const uint32_t _address) override { return owner(_address)->read8(_address); }
			void write8(const uint32_t _address, const uint8_t _value) override { owner(_address)->write8(_address, _value); }
		private:
			struct Entry { uint32_t address, length; emu::Device* device; };
			emu::Device* owner(const uint32_t _address) const
			{
				for (const auto& e : m_entries)
					if (_address >= e.address && _address < e.address + e.length) return e.device;
				return m_fallback;
			}
			std::vector<Entry> m_entries;
			emu::Device* m_fallback;
		};

		// Everything the board does not decode. Reports each address once.
		class CatchAllDevice final : public emu::Device
		{
		public:
			uint8_t read8(uint32_t _address) override;
			void write8(uint32_t _address, uint8_t _value) override;
		private:
			void report(const char* _access, uint32_t _address);
			std::unordered_set<uint32_t> m_reported;
		};

		// Status byte next to the rack's ASIC windows: one DINT bit per chip
		template<typename TAsics>
		class DintStatus final : public emu::Device
		{
		public:
			explicit DintStatus(const TAsics& _asics) : m_asics(_asics) {}
			uint8_t read8(uint32_t) override { return m_asics.readDintLines(); }
			void write8(uint32_t, uint8_t) override {}
		private:
			const TAsics& m_asics;
		};
	}
}
