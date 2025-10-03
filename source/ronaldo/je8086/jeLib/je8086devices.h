#pragma once

#include <functional>
#include <h8s/h8s.hpp>

#include "esp/esp.hpp"

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

		class MultiAsic : public H8SDevice
		{
		public:
			void setPostSample(const std::function<void(int32_t, int32_t)>& _postSample) { postSample = _postSample; }

			void dump() {
				asic0.dump("dumps/asic0.bin", "dumps/asic0.txt");
				asic1.dump("dumps/asic1.bin", "dumps/asic1.txt");
				asic2.dump("dumps/asic2.bin", "dumps/asic2.txt");
				asic3.dump("dumps/asic3.bin", "dumps/asic3.txt");
			}

			uint8_t read(uint32_t _address) override
			{
				const int asic = (_address >> 14) & 3; _address &= 0x3fff;
				if (asic == 0) return asic0.readuC(_address);
				if (asic == 1) return asic1.readuC(_address);
				if (asic == 2) return asic2.readuC(_address);
				return asic3.readuC(_address);
			}

			void write(uint32_t _address, uint8_t _value) override
			{
				const int asic = (_address >> 14) & 3; _address &= 0x3fff;
				if (asic == 0) asic0.writeuC(_address, _value);
				else if (asic == 1) asic1.writeuC(_address, _value);
				else if (asic == 2) asic2.writeuC(_address, _value);
				else asic3.writeuC(_address, _value);
			}
			
			bool tick()
			{
				uint64_t now = (state->getCycles() * 1323ull) / 625ull; // Convert from uC cycles to DSP steps. (this is (clockrate / 2) / (uc clock = 16000000), simplified)
				uint64_t diff = now - lastCycles;
				if (diff + cycles_this_sample > stepsPerFS) {
					diff = stepsPerFS - cycles_this_sample; // are we crossing a sample boundary?
				}
				lastCycles += diff;
				cycles_this_sample += diff;
				for (int i = 0; i < diff; i++) asic0.step_cores(); // clock the cores.
				for (int i = 0; i < diff; i++) asic1.step_cores(); // clock the cores.
				for (int i = 0; i < diff; i++) asic2.step_cores(); // clock the cores.
				for (int i = 0; i < diff; i++) asic3.step_cores(); // clock the cores.
				if (cycles_this_sample < stepsPerFS) return false;

				postSample(asic3.readGRAM(0xe8), asic3.readGRAM(0xec));
				
				for (int k = 0; k <= 0x4; k += 2) asic1.writeGRAM(asic0.readGRAM(0x80 + k), k);
				for (int k = 0; k <= 0xa; k += 2) asic2.writeGRAM(asic1.readGRAM(0x80 + k), k);
				for (int k = 0; k <= 0xe; k += 2) asic3.writeGRAM(asic2.readGRAM(0x80 + k), k);

				asic3.writeGRAM(asic2.readGRAM(0xa0), 0x20);
				asic3.writeGRAM(asic2.readGRAM(0xa2), 0x22);
				cycles_this_sample = 0;
				asic0.sync_cores();
				asic1.sync_cores();
				asic2.sync_cores();
				asic3.sync_cores();
				return true;
			}

			void runForCycles(uint64_t cycles) {
				uint64_t diff = cycles + cyclesResidual - lastCycles;
				lastCycles = cycles;

				uint64_t samples = diff / (768/2);
				cyclesResidual = diff % (768/2);

				for (int i = 0; i < samples; i++) {
					// for (size_t j = 0; j < (768/2); j++) asic0.step_cores();
					// for (size_t j = 0; j < (768/2); j++) asic1.step_cores();
					// for (size_t j = 0; j < (768/2); j++) asic2.step_cores();
					// for (size_t j = 0; j < (768/2); j++) asic3.step_cores();

					asic0.opt.genProgramIfDirty();
					asic1.opt.genProgramIfDirty();
					asic2.opt.genProgramIfDirty();
					asic3.opt.genProgramIfDirty();

					asic0.opt.callOptimized(&asic0);
					asic1.opt.callOptimized(&asic1);
					asic2.opt.callOptimized(&asic2);
					asic3.opt.callOptimized(&asic3);

					postSample(asic3.readGRAM(0xe8), asic3.readGRAM(0xec));
					
					for (int k = 0; k <= 0x4; k += 2) asic1.writeGRAM(asic0.readGRAM(0x80 + k), k);
					for (int k = 0; k <= 0xa; k += 2) asic2.writeGRAM(asic1.readGRAM(0x80 + k), k);
					for (int k = 0; k <= 0xe; k += 2) asic3.writeGRAM(asic2.readGRAM(0x80 + k), k);

					asic3.writeGRAM(asic2.readGRAM(0xa0), 0x20);
					asic3.writeGRAM(asic2.readGRAM(0xa2), 0x22);

					asic0.sync_cores();
					asic1.sync_cores();
					asic2.sync_cores();
					asic3.sync_cores();
				}
			}
		protected:
			ESP<17> asic0;
			ESP<0> asic1, asic2;
			ESP<19> asic3; // should really be 18, but it works only with 19
			enum {stepsPerFS = 384};
			std::function<void(int32_t, int32_t)> postSample;
			uint64_t lastCycles = 0, cyclesResidual = 0;
			uint32_t cycles_this_sample {0};
		};

		class Port : public H8SDevice
		{
		public:
			Port(std::function<void(Port*)>&& _onLedsChanged = [](Port*){}) : onLedsChanged(std::move(_onLedsChanged)) {releaseAll();}
			void releaseAll()
			{
				for (auto& i : data) i = static_cast<int8_t>(0xff);
			}
			void flashMode()
			{
				press(kSwitch_LFO1Waveform);
				press(kSwitch_Osc1Waveform);
				press(kSwitch_Osc2Waveform);
				press(kSwitch_Sync);
			}
			void versionNumber()
			{
				press(kSwitch_1);
				press(kSwitch_3);
				press(kSwitch_LFO1Waveform);
			}
			void enterTestMode()
			{
				press(kSwitch_Sync);
				press(kSwitch_TVF24);
				press(kSwitch_TVFType);
			}
			void press(int which, bool down = true)
			{
				int bit = 1 << (which & 7); which >>= 3;
				if (down) data[which] &= ~bit;
				else data[which] |= bit;
			}
			virtual uint8_t read(uint32_t address) {
				if (address == 0xffffd4) return portBDDR;
				char id = (address == 0xffffd3) ? 'A' : 'B';
				int which = portAstate & 31;
				return (id=='A') ? portAstate : data[which];
			}
			virtual void write(uint32_t address, uint8_t value) {
				if (address==0xffffd4) {portBDDR = value; return;}
				char id = (address == 0xffffd3) ? 'A' : 'B';
				if (id=='A')
				{
					if (value & 32) latchA = true;
					if (!(value & 32) && latchA)
					{
						latch = value & 31; latchA = false;
						bool diff = leds[latch] != portBDR;
						leds[latch] = portBDR;
						if (diff) onLedsChanged(this);
					}
					portAstate = value;
				}
				if (id == 'B') portBDR = value;
			}

			static int getLedId(const uint32_t _index) {return lits[_index];}

			bool getLed(const uint32_t _i) const { const int w = getLedId(_i); return (leds[w >> 3] & (1 << (w & 7))); }
		protected:
			static int lits[];
			static const char* const litnames[66];
			int8 data[32] {}, leds[32] {}; int latch {0};bool latchA {false};
			int8 portAstate {0}, portBDDR {0}, portBDR;
			std::function<void(Port*)> onLedsChanged;

			void dumpLEDs() const { for (int i = 0; i < 66; i++) if (getLed(i)) printf("%s, ", litnames[i]); printf("\n"); }
		};

		class KeyScanner : public H8SDevice
		{
		public:
			uint8_t read(uint32_t _address) override { return 0; }
			void write(uint32_t _address, uint8_t _value) override {}
		};

		class Faders : public H8SDevice
		{
		public:
			Faders() {for (int i = 0; i < 64; i++) values[i] = 512; values[kFader_BattSense] = 512;}
			virtual uint8_t read(uint32_t address) {
				if (address == 0xffffcb) return p6dr;
				if (address == 0xffffe8) return adcsr;
				if (address >= 0xffffe0 && address < 0xffffe8)
				{
					int off = (address - 0xffffe0) & 7;
					int which = scanning | ((off << 2) & 0x18);	// [1: AN8-4/3-0][2: 0/3][3: ADC2,1,0] = [3: AN line][3: ADC2,1,0 value]
					if ((which & 0x38) == 0) which = 0;			// any multiple of 8 is always pitch bend
					if ((which & 0x38) == 8) which &= 0x3b;		//mod/expdl/battsens, ignores ADC2
					if ((which & 0x3C) == 40) which = 40;		// osc1ctrl2 mapped across several pins
					if ((which & 0x3e) == 44) which = 44;		// osc1ctrl1 mapped across several pins
					if ((which & 0x3e) == 46) which = 46;		// finetune mapped across several pins

					int val = values[which] & 1023;
					return (off & 1) ? ((val << 6) & 0xc0) : ((val >> 2) & 0xff);
				}
				return 0;
			}
			virtual void write(uint32_t address, uint8_t value)
			{
				if (address == 0xffffcb)	{scanning = ((value & 7)) | (scanning & 32); p6dr = value;}
				if (address == 0xffffe8)	{scanning = (scanning & 7) | ((value << 3) & 32);adcsr = value;}
			}
			void setFader(int which, int value)	// 0<=which<64, value 0->1023!!
			{
				// which:
				// 0 = pitch bend, 8 = mod, 9 = EXPDL, 10 = 0, 11 = BATSENS
				// n >=16 = VR(n - 15).
				// e.g. cutoff = VR12, so n = 12 + 15 = 27. See datasheet.
				values[which] = value;
			}
		protected:
			int8 scanning {0}, p6dr {0}, adcsr {0};
			int values[64] {};
		};
	}
}
