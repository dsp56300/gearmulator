#pragma once

#include "xp_dsp_program.h"
#include "xp_dsp_state.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace xpLib
{
	class DspJitDispatcher;

	namespace dspNaive
	{
		// The reference engine: decodes the raw PRAM/CRAM words of the current slot every time it runs and keeps
		// every runtime counter the chip has. Executes one slot; returns false once the frame's budget is spent.
		bool executeSlot(DspState& _state, const DspProgram& _program, DspFrameContext& _context,
						 bool _consumeStagedBusA);
	} // namespace dspNaive

	// Host program, runtime state and frame scheduling for the effects DSP.
	// Background compilation uses the interpreter until compiled code is ready.
	class Dsp
	{
	public:
		static constexpr size_t nProgramSlots = dsp::nProgramSlots;
		static constexpr size_t nExecutionSlots = dsp::nExecutionSlots;
		static constexpr size_t nIramSlots = dsp::nIramSlots;
		static constexpr size_t nEramWords = dsp::nEramWords;
		static constexpr size_t nSerialWords = dsp::nSerialWords;
		static constexpr size_t nSerialBuses = dsp::nSerialBuses;
		static constexpr uint16_t alternateBcdPacking_3932 = dsp::alternateBcdPacking_3932;

		using ProgramRam = std::array<uint32_t, nProgramSlots>;
		using CoefficientRam = std::array<uint16_t, nProgramSlots>;
		using InternalRam = std::array<uint32_t, nIramSlots>;
		using ExternalRam = std::array<uint32_t, nEramWords>;
		using SerialFrame = std::array<int32_t, nSerialWords>;
		using MixerSend = DspMixerSend;
		using MixerFrame = DspMixerFrame;
		using StepRequest = DspStepRequest;
		using SerialBus = DspSerialBus;

		Dsp();
		Dsp(const Dsp& _other);
		Dsp& operator=(const Dsp& _other);
		~Dsp();

		void reset();
		void beginMixerFrame();
		void replaceMixer(size_t _destination, int64_t _value);
		void accumulateMixer(size_t _destination, int64_t _value);
		static uint8_t iram3PartitionCount(uint16_t _serialAudio1Config);
		void setIram3Target(size_t _index, uint16_t _target, uint8_t _partitionCount = 0);
		void stepIram3Ramps(const std::array<uint16_t, 4>& _rates, uint8_t _partitionCount, uint8_t _roundingPhase);
		void setSerialInput(SerialBus _bus, const int32_t* _words, size_t _count);
		static uint32_t encodeBcdPacket(int32_t _word, uint16_t _serialFormat_3932);
		void step(bool _executeProgram, bool _serialInputEnabled = true, uint16_t _serialAudio0Config = 0xffff,
				  uint16_t _serialAudio1Config = 0xffff, bool _serialOutputEnabled = true,
				  size_t _executionSlots = nExecutionSlots, const MixerFrame* _mixerFrame = nullptr);
		void step(const StepRequest& _request);
		// Execute two synchronously clocked XPs one DSP slot at a time. This is the logical complete-word
		// link: it deliberately bypasses physical SDOA lane serialization. A completed word becomes visible
		// at the peer's SDIA node immediately after the producing slot, not in the next sample frame.
		static void stepLinked(Dsp& _a, const StepRequest& _aRequest, Dsp& _b, const StepRequest& _bRequest);

		// Program writes. A CRAM write on a coefficient/immediate slot and a PRAM write that only moves an
		// ERAM offset are applied in place; anything else marks the program for a new lowering, which
		// syncProgram() performs before the next frame that needs it.
		void writePram(size_t _slot, uint32_t _value);
		void writeCram(size_t _slot, uint16_t _value);
		// Lowers the program for the frame configuration of _request when the program or the configuration
		// changed; every lowering bumps the generation.
		void syncProgram(const StepRequest& _request, bool _consumeStagedBusA = true, bool _linked = false);
		// Applies the mixer sends of _request's frame to the mixer bank before the frame when the program never
		// touches a deposited cell, summing per cell where no intermediate saturation occurs and replaying
		// the sends in order where it does; then clears the request's frame so the engines skip the deposits.
		// Returns false, leaving the request alone, when a deposited cell is live.
		bool hoistDeposits(StepRequest& _request, const DspMixerSummary& _summary, bool _linked = false);

		// Mutable program access invalidates the compiled program.
		ProgramRam& pram()
		{
			m_programTainted = true;
			return m_program.pram;
		}
		const ProgramRam& pram() const { return m_program.pram; }
		CoefficientRam& cram()
		{
			m_programTainted = true;
			return m_program.cram;
		}
		const CoefficientRam& cram() const { return m_program.cram; }
		const DspProgram& program() const { return m_program; }
		InternalRam& iram1() { return m_state.iram1; }
		const InternalRam& iram1() const { return m_state.iram1; }
		InternalRam& iram2() { return m_state.iram2; }
		const InternalRam& iram2() const { return m_state.iram2; }
		InternalRam& iram3() { return m_state.iram3; }
		const InternalRam& iram3() const { return m_state.iram3; }
		ExternalRam& eram() { return m_state.eram; }
		const ExternalRam& eram() const { return m_state.eram; }

		// The complete runtime state, shared by every engine.
		DspState& state() { return m_state; }
		const DspState& state() const { return m_state; }

		uint32_t eramPos() const { return m_state.eramPos; }
		bool iramSelPhase() const { return m_state.iramSelPhase != 0; }
		const SerialFrame& serialOutput(const SerialBus _bus) const
		{
			return m_state.serialOutput[static_cast<size_t>(_bus)];
		}
		size_t serialOutputCount(const SerialBus _bus) const
		{
			return m_state.serialOutputCount[static_cast<size_t>(_bus)];
		}

		// Only buses A and B have inputs. These views expose the frame staged by the board and the receive
		// node left after the most recent DSP pass.
		const SerialFrame& serialInput(const SerialBus _bus) const
		{
			return m_state.serialInput[static_cast<size_t>(_bus)];
		}
		size_t serialInputCount(const SerialBus _bus) const
		{
			return m_state.serialInputCount[static_cast<size_t>(_bus)];
		}
		int32_t serialInputNode(const SerialBus _bus) const
		{
			return static_cast<int32_t>(m_state.serialInputNode[static_cast<size_t>(_bus)]);
		}

		uint8_t outputPins() const { return m_state.outputPins; }

	private:
		// One frame on the naive engine.
		void runFrame(DspState& _state, const StepRequest& _request);
		// Two chips in lockstep on the naive engine.
		static void stepLinkedEngines(Dsp& _a, DspState& _aState, const StepRequest& _aRequest, Dsp& _b,
									  DspState& _bState, const StepRequest& _bRequest);
		DspJitDispatcher& jit();
		// Runs one frame on the compiled code when it is available, else on the naive engine.
		void runJitFrame(const StepRequest& _request);
		static void stepLinkedJit(Dsp& _a, const StepRequest& _aRequest, Dsp& _b, const StepRequest& _bRequest);

		DspProgram m_program{};
		DspState m_state{};
		DspParams m_params{};
		FlatProgram m_flat{};
		uint64_t m_generation = 0;
		bool m_programTainted = true;
		std::unique_ptr<DspJitDispatcher> m_jit;  // jit engines only; not copied
		std::unique_ptr<DspJitDispatcher> m_link; // the lockstep pair function, owned by the first chip
		const Dsp* m_linkPartner = nullptr;
	};
} // namespace xpLib
