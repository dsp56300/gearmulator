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

		/* Stage-scoped state. thread_local, not plain global: with fork every stage
		 * got a private copy of the whole emulator for free, but a THREADED
		 * pipeline runs all stages in one address space and each needs its own
		 * bounds and handoff callbacks. Fork is unaffected -- the child continues
		 * on the thread that inherited these values.
		 *
		 * Fork-parallel mode (stage-local, no race).
		 * 0 = all ASICs (default), 1 = parent: H8S + ASICs [0, split),
		 * 2 = child: ASICs [split, 4). */
		inline thread_local int g_je_parallel_mode = 0;
		/* First ASIC owned by the child. 2 (ASIC0+1 | ASIC2+3) is the historical split;
		 * 1 (ASIC0 | ASIC1+2+3) balances the halves — the H8S is the parent's real cost.
		 * Must be set before the fork; only 1 and 2 are supported (the 2->3 handoff
		 * carries 10 words, more than the ring payload). */
		inline int g_je_split_asic = 2;
		/* Pipeline stage owned by THIS process: ASICs [lo, hi). The parent is
		 * [0, g_je_split_asic); a child stage may be any contiguous run above it,
		 * so a 3-stage {0}/{1}/{2,3} pipeline is the 2-stage code with different
		 * bounds. Process-local after fork. */
		inline thread_local int g_je_stage_lo = 0;
		inline thread_local int g_je_stage_hi = 4;
		/* Words handed from ASIC n to ASIC n+1 at GRAM 0x80.. (dest 0..), per boundary.
		 * The 2->3 boundary also carries 0xa0/0xa2 -> 0x20/0x22, packed as words 8, 9
		 * of the handoff payload (JE_HANDOFF_MAX). */
		inline constexpr int g_je_handoff_words[3] = {3, 6, 8};
		inline constexpr int JE_HANDOFF_MAX = 10;
		/* Mode 1: called per sample with ASIC(split-1)'s handoff values
		 * (g_je_handoff_words[split-1] of them). */
		/* Audio out for the stage that owns ASIC3. postSample is a MultiAsic
		 * MEMBER, so with fork each stage had its own; threads share one object
		 * and the last stage's audio push would also be invoked by stage 0's
		 * dummy call below. Stage-scoped hook instead: set it and the owning
		 * stage uses it, leave it unset and postSample behaves as before. */
		inline thread_local std::function<void(int32_t, int32_t)> g_je_stage_audio_out;
		/* Mode 1 hands Device::process() a silent sample per rendered sample so it
		 * keeps its expected count while the real audio is produced elsewhere --
		 * correct when "elsewhere" is another PROCESS. When the stages are threads
		 * the parent drains their audio into that same buffer itself, so the dummy
		 * would interleave a zero with every real sample. Cleared by JePipeline. */
		inline thread_local bool g_je_parent_dummy_audio = true;
		inline thread_local std::function<void(const int32_t*)> g_je_gram_produce;
		/* Mode 2: called per sample to receive those values.
		 * Returns false if shutdown requested. */
		inline thread_local std::function<bool(int32_t*)> g_je_gram_consume;
		/* Mode 1: called when H8S writes to a child-owned ASIC's registers (PRAM
		 * programming). Forwards the write to the child so it sees patch changes. */
		inline thread_local std::function<void(int asic, uint32_t addr, uint8_t val)> g_je_uc_write_forward;

		/* The parent's copy of the readback registers of ASICs it does NOT own.
		 * With fork every stage had a private copy of the whole emulator, so the
		 * H8S could read any ASIC directly. Threads share one set of objects, so a
		 * direct read would race with the owning stage running that ASIC -- the
		 * mirror image of the guard write() already has. The owning stage publishes
		 * its four bytes and the parent reads them from here instead. */
		inline thread_local uint8_t g_je_parent_readback[4][4] = {};



		class MultiAsic : public H8SDevice
		{
		public:
			void setPostSample(const std::function<void(int32_t, int32_t)>& _postSample) { postSample = _postSample; }
			/* Real audio out: tap first, then the normal sink. */
			void emitAudio(int32_t _l, int32_t _r) {
				postSample(_l, _r);
			}

			/* Apply a forwarded uC write to a child-owned ASIC (child process) */
			void applyUcWrite(int asic, uint32_t addr, uint8_t val) {
				if (asic < g_je_stage_lo || asic >= g_je_stage_hi) return;
				forAsic(asic, [&](auto& a) { a.writeuC(addr, val); });
			}

			/* Readback register forwarding for fork-parallel mode.
			 * Child exports its ASICs' readback regs so the parent's H8S
			 * sees current values instead of stale snapshot copies.
			 * 4 bytes per ASIC. */
			void getReadback(int asic, uint8_t *out) const {
				forAsic(asic, [&](auto& a) { a.getReadbackRegs(out); });
			}
			void setReadback(int asic, const uint8_t *in) {
				forAsic(asic, [&](auto& a) { a.setReadbackRegs(in); });
			}
			/* Parent-side: store a stage's published readbacks where read() will find
			 * them, WITHOUT touching the ASIC object that stage is running. */
			static void setParentReadback(int asic, const uint8_t *in) {
				for (int i = 0; i < 4; i++) g_je_parent_readback[asic][i] = in[i];
			}
			void getAsic23Readback(uint8_t *a2, uint8_t *a3) const { getReadback(2, a2); getReadback(3, a3); }
			void setAsic23Readback(const uint8_t *a2, const uint8_t *a3) { setReadback(2, a2); setReadback(3, a3); }

			/* Boundary from -> from+1 (from = 0, 1, 2), same reads/writes as the serial path. */
			void handoff(int from) {
				const int n = g_je_handoff_words[from];
				forAsic(from, [&](auto& src) {
					forAsic(from + 1, [&](auto& dst) {
						for (int k = 0; k < n; k++) dst.writeGRAM(src.readGRAM(0x80 + k * 2), k * 2);
						if (from == 2) {
							dst.writeGRAM(src.readGRAM(0xa0), 0x20);
							dst.writeGRAM(src.readGRAM(0xa2), 0x22);
						}
					});
				});
			}
			void readHandoff(int from, int32_t *gram) {
				forAsic(from, [&](auto& src) {
					for (int k = 0; k < g_je_handoff_words[from]; k++) gram[k] = src.readGRAM(0x80 + k * 2);
					if (from == 2) { gram[8] = src.readGRAM(0xa0); gram[9] = src.readGRAM(0xa2); }
				});
			}
			void writeHandoff(int to, const int32_t *gram) {
				forAsic(to, [&](auto& dst) {
					for (int k = 0; k < g_je_handoff_words[to - 1]; k++) dst.writeGRAM(gram[k], k * 2);
					if (to == 3) { dst.writeGRAM(gram[8], 0x20); dst.writeGRAM(gram[9], 0x22); }
				});
			}
			void runAsics(int first, int last) {
				for (int i = first; i < last; i++) forAsic(i, [](auto& a) { a.opt.genProgramIfDirty(); });
				for (int i = first; i < last; i++) forAsic(i, [](auto& a) { a.opt.callOptimized(&a); });
			}
			void syncAsics(int first, int last) {
				for (int i = first; i < last; i++) forAsic(i, [](auto& a) { a.sync_cores(); });
			}

			/* Process a single ASIC2+3 sample driven by external GRAM data.
			 * Called directly by fork child — no H8S needed.
			 *
			 * Ordering matches serial mode (mode 0):
			 *   1. Run ASICs with GRAM from PREVIOUS handoff
			 *   2. postSample (read audio output)
			 *   3. Consume new GRAM from ring → write to ASIC2
			 *   4. ASIC2→ASIC3 handoff
			 *   5. sync_cores
			 * This ensures ASIC2 always processes one-sample-old GRAM,
			 * exactly like the serial path where handoff follows the run. */
			bool processSampleAsic23() { return processSampleChild(); }
			bool processSampleChild() {
				const int lo = g_je_stage_lo, hi = g_je_stage_hi;
				// 1. Run this stage's ASICs with existing GRAM (from previous iteration or snapshot)
				runAsics(lo, hi);

				// 2. Output: audio if this stage holds ASIC3, else the handoff to the
				//    next stage — read PRE-sync, same point as the parent's produce.
				if (hi == 4) {
					const int32_t l = asic3.readGRAM(0xe8), r = asic3.readGRAM(0xec);
					if (g_je_stage_audio_out) g_je_stage_audio_out(l, r);
					else postSample(l, r);
				} else if (g_je_gram_produce) {
					int32_t gram[JE_HANDOFF_MAX];
					readHandoff(hi - 1, gram);
					g_je_gram_produce(gram);
				}

				// 3. Consume new GRAM from the previous stage (the lo-1 -> lo handoff)
				if (g_je_gram_consume) {
					int32_t gram[JE_HANDOFF_MAX];
					if (!g_je_gram_consume(gram)) return false;
					writeHandoff(lo, gram);
				}

				// 4. Remaining handoffs inside this stage (for next sample)
				for (int b = lo; b < hi - 1; b++) handoff(b);

				// 5. sync_cores
				syncAsics(lo, hi);
				return true;
			}

		void dump() {
				asic0.dump("dumps/asic0.bin", "dumps/asic0.txt");
				asic1.dump("dumps/asic1.bin", "dumps/asic1.txt");
				asic2.dump("dumps/asic2.bin", "dumps/asic2.txt");
				asic3.dump("dumps/asic3.bin", "dumps/asic3.txt");
			}

			uint8_t read(uint32_t _address) override
			{
				const int asic = (_address >> 14) & 3; _address &= 0x3fff;
				// Mirror of the guard in write(): the parent owns [0, split) only, and
				// reading an ASIC another thread is running is a data race. Answer from
				// the snapshot that stage published instead.
				if (g_je_parallel_mode == 1 && asic >= g_je_split_asic)
					return g_je_parent_readback[asic][_address & 3];
				uint8_t v = 0;
				forAsic(asic, [&](auto& a) { v = a.readuC(_address); });
				return v;
			}

			void write(uint32_t _address, uint8_t _value) override
			{
				const int asic = (_address >> 14) & 3; _address &= 0x3fff;
				// In parallel mode the parent owns [0, split) only; a write to a
				// child-owned ASIC is forwarded, not applied to the parent's stale copy.
				if (g_je_parallel_mode == 1 && asic >= g_je_split_asic) {
					if (g_je_uc_write_forward) g_je_uc_write_forward(asic, _address, _value);
					return;
				}
				forAsic(asic, [&](auto& a) { a.writeuC(_address, _value); });
			}
			
			void runForCycles(uint64_t cycles) {
				uint64_t diff = cycles + cyclesResidual - lastCycles;
				lastCycles = cycles;

				/* Called once per H8S instruction; a sample boundary is crossed on
				 * one call in ~40, so skip the div/mod (and the loop) otherwise. */
				if (diff < (768/2)) { cyclesResidual = diff; return; }
				uint64_t samples = diff / (768/2);
				cyclesResidual = diff % (768/2);

				for (int i = 0; i < samples; i++) {
					const int mode = g_je_parallel_mode;

					if (mode == 0) {
						// Original serial path — preserved exactly.
						// All 4 ASICs run with GRAM from previous sample,
						// then GRAM handoff writes values for next sample.
						asic0.opt.genProgramIfDirty();
						asic1.opt.genProgramIfDirty();
						asic2.opt.genProgramIfDirty();
						asic3.opt.genProgramIfDirty();

						asic0.opt.callOptimized(&asic0);
						asic1.opt.callOptimized(&asic1);
						asic2.opt.callOptimized(&asic2);
						asic3.opt.callOptimized(&asic3);

						emitAudio(asic3.readGRAM(0xe8), asic3.readGRAM(0xec));

						for (int k = 0; k <= 0x4; k += 2) asic1.writeGRAM(asic0.readGRAM(0x80 + k), k);
						for (int k = 0; k <= 0xa; k += 2) asic2.writeGRAM(asic1.readGRAM(0x80 + k), k);
						for (int k = 0; k <= 0xe; k += 2) asic3.writeGRAM(asic2.readGRAM(0x80 + k), k);
						asic3.writeGRAM(asic2.readGRAM(0xa0), 0x20);
						asic3.writeGRAM(asic2.readGRAM(0xa2), 0x22);

						asic0.sync_cores();
						asic1.sync_cores();
						asic2.sync_cores();
						asic3.sync_cores();
					} else if (mode == 1) {
						// Parent process: ASICs [0, split).
						// Must mirror mode 0's ordering: the handoff READ happens
						// BEFORE sync_cores. GRAM is a rotating buffer indexed by
						// (offset + iramPos), and sync_cores decrements iramPos,
						// so a post-sync read returns the previous sample's
						// neighbouring slot — silently producing data shifted by
						// one GRAM address.
						const int split = g_je_split_asic;
						runAsics(0, split);
						for (int b = 0; b < split - 1; b++) handoff(b);
						// Read the last parent ASIC's outputs PRE-sync (same point as serial mode 0)
						if (g_je_gram_produce) {
							int32_t gram[JE_HANDOFF_MAX];
							readHandoff(split - 1, gram);
							g_je_gram_produce(gram);
						}
						syncAsics(0, split);
						// Dummy postSample so Device::process() gets its
						// expected audio output and doesn't spin forever
						if (g_je_parent_dummy_audio)
							postSample(0, 0);
					} else {
						// Child process driven through the H8S (unused by the fork
						// paths, which call processSampleChild directly).
						const int split = g_je_split_asic;
						if (g_je_gram_consume) {
							int32_t gram[JE_HANDOFF_MAX];
							if (!g_je_gram_consume(gram)) break;
							writeHandoff(split, gram);
						}
						runAsics(split, 4);
						for (int b = split; b < 3; b++) handoff(b);
						emitAudio(asic3.readGRAM(0xe8), asic3.readGRAM(0xec));
						syncAsics(split, 4);
					}
				}
			}
		protected:
			ESP<17> asic0;
			ESP<0> asic1, asic2;
			ESP<19> asic3; // should really be 18, but it works only with 19
			// The four ASICs are three distinct template types, so dispatch by index.
			template <class F> void forAsic(int i, F&& f) {
				switch (i) { case 0: f(asic0); break; case 1: f(asic1); break; case 2: f(asic2); break; default: f(asic3); break; }
			}
			template <class F> void forAsic(int i, F&& f) const {
				switch (i) { case 0: f(asic0); break; case 1: f(asic1); break; case 2: f(asic2); break; default: f(asic3); break; }
			}
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
