#include "xp_dsp.h"

#include "xp_dsp_jit.h"
#include "xp_dsp_ops.h"

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace xpLib
{
	namespace
	{
		// std::countr_zero is C++20 and xpLib builds as C++17, so this is the
		// portable stand-in for __builtin_ctzll, which MSVC does not have.
		// Only ever called with a non-zero mask.
		size_t countTrailingZeros(const uint64_t _mask)
		{
#ifdef _MSC_VER
			unsigned long index;
			_BitScanForward64(&index, _mask);
			return static_cast<size_t>(index);
#else
			return static_cast<size_t>(__builtin_ctzll(_mask));
#endif
		}
	}

	Dsp::Dsp() = default;

	Dsp::Dsp(const Dsp& _other)
		: m_program(_other.m_program), m_state(_other.m_state), m_params(_other.m_params), m_flat(_other.m_flat),
		  m_generation(_other.m_generation), m_programTainted(_other.m_programTainted)
	{
	}

	Dsp& Dsp::operator=(const Dsp& _other)
	{
		if (this == &_other)
			return *this;
		m_program = _other.m_program;
		m_state = _other.m_state;
		m_params = _other.m_params;
		m_flat = _other.m_flat;
		m_generation = _other.m_generation;
		m_programTainted = _other.m_programTainted;
		return *this;
	}

	Dsp::~Dsp() = default;

	void Dsp::reset()
	{
		m_program = DspProgram{};
		dspStateReset(m_state);
		m_params = DspParams{};
		m_flat = FlatProgram{};
		m_programTainted = true;
	}

	DspJitDispatcher& Dsp::jit()
	{
		if (!m_jit)
		{
			m_jit = std::make_unique<DspJitDispatcher>();
		}
		return *m_jit;
	}

	void Dsp::writePram(const size_t _slot, const uint32_t _value)
	{
		if (_slot >= nProgramSlots)
			return;
		const auto previous = m_program.pram[_slot];
		m_program.pram[_slot] = _value & 0x0fffffff;
		if (m_programTainted)
			return;
		if (dspPramWriteIsStructural(previous, m_program.pram[_slot]))
			m_programTainted = true;
		else
			dspRefreshEramOffsets(m_program, m_params, _slot);
	}

	void Dsp::writeCram(const size_t _slot, const uint16_t _value)
	{
		if (_slot >= nProgramSlots)
			return;
		m_program.cram[_slot] = _value;
		if (m_programTainted)
			return;
		if (dspCramWriteIsStructural(m_program.pram[_slot]))
			m_programTainted = true;
		else
			m_params.param[_slot] = dspDecodeParam(m_program.pram[_slot], _value);
	}

	void Dsp::syncProgram(const StepRequest& _request, const bool _consumeStagedBusA, const bool _linked)
	{
		const auto config = dspFrameConfig(_request, _consumeStagedBusA, _linked);
		if (!m_programTainted && config == m_flat.config)
			return;
		flatLower(m_program, config, m_flat, m_params);
		m_flat.generation = ++m_generation;
		m_programTainted = false;
	}

	bool Dsp::hoistDeposits(StepRequest& _request, const DspMixerSummary& _summary, const bool _linked)
	{
		if (_request.mixerFrame == nullptr)
			return false;
		syncProgram(_request, !_linked, _linked);
		if (_request.executeProgram && (_summary.seen & m_flat.mixerCells[m_state.iramSelPhase & 1]) != 0)
			return false;
		auto& bank = dspOps::mixerIram(m_state);
		auto seen = _summary.seen;
		while (seen != 0)
		{
			const auto cell = countTrailingZeros(seen);
			const auto bit = uint64_t{1} << cell;
			seen &= ~bit;
			if ((_summary.clipped & bit) == 0)
			{
				bank[cell] = static_cast<uint32_t>(_summary.sums[cell]) & 0x00ffffff;
				continue;
			}
			// The running sum saturated somewhere: replay this cell's sends in deposit order.
			static constexpr std::array<size_t, 4> sendCycle = {0, 2, 2, 3};
			const auto cycles = std::min(_request.executionSlots, nExecutionSlots);
			const auto& frame = *_request.mixerFrame;
			bank[cell] = 0;
			for (size_t voice = 0; voice * 4 < cycles; ++voice)
				for (size_t send = 0; send < 4; ++send)
				{
					const auto& event = frame[voice][send];
					if (event.destination == cell && voice * 4 + sendCycle[send] < cycles)
						bank[cell] = dspOps::encode24(dspOps::signExtend(bank[cell], 24) + event.contribution);
				}
		}
		m_state.mixerInitialized |= _summary.seen;
		_request.mixerFrame = nullptr;
		return true;
	}

	void Dsp::beginMixerFrame() { dspOps::beginMixerFrame(m_state); }

	void Dsp::replaceMixer(const size_t _destination, const int64_t _value)
	{
		dspOps::replaceMixer(m_state, _destination, _value);
	}

	void Dsp::accumulateMixer(const size_t _destination, const int64_t _value)
	{
		dspOps::accumulateMixer(m_state, _destination, _value);
	}

	uint8_t Dsp::iram3PartitionCount(const uint16_t _serialAudio1Config)
	{
		return dspOps::iram3PartitionCount(_serialAudio1Config);
	}

	void Dsp::setIram3Target(const size_t _index, const uint16_t _target, const uint8_t _partitionCount)
	{
		if (_index >= nIramSlots)
			return;
		// The target aperture has 64 host words but only five address bits reach the parameter RAM: its
		// upper half aliases its lower half.
		const auto parameter = _index & 0x1f;
		const auto slot = parameter | 0x20;
		const auto target = static_cast<uint16_t>(_target & 0x03ff);
		if (parameter < _partitionCount)
		{
			m_state.iram3[slot] = target;
			return;
		}
		m_state.iram3[slot] = (m_state.iram3[slot] & ~uint32_t{0x03ff}) | target;
	}

	void Dsp::stepIram3Ramps(const std::array<uint16_t, 4>& _rates, const uint8_t _partitionCount,
							 const uint8_t _roundingPhase)
	{
		const auto reverseFourBits = [](uint8_t _value)
		{
			_value = static_cast<uint8_t>(((_value & 0x5) << 1) | ((_value & 0xa) >> 1));
			return static_cast<uint8_t>((_value << 2) | (_value >> 2)) & 0xf;
		};
		const auto threshold = reverseFourBits(_roundingPhase & 0xf);
		for (size_t parameter = _partitionCount; parameter < nIramSlots / 2; ++parameter)
		{
			const auto slot = parameter | 0x20;
			const auto cell = m_state.iram3[slot] & 0x03ffffff;
			const auto low = cell & 0x03ff;
			const auto current = dspOps::signExtend(cell >> 10, 16);
			// 0x200 is the unused most-negative target code. XP3 leaves the current untouched for every
			// tested seed and rate when it is selected.
			if (low == 0x0200)
				continue;
			const auto exactTarget = dspOps::signExtend(low, 10) << 6;
			const auto direction = exactTarget - current;
			if (direction == 0)
				continue;
			// Reducing the repeated 26-bit target to the signed 16-bit parameter datapath adds the
			// otherwise easy-to-miss +1 only for positive, non-maximum target codes.
			const auto roundedTarget = exactTarget + (low > 0 && low < 0x01ff);
			auto difference = roundedTarget - current;
			// One current unit above the exact target coincides with the upward-rounded target. The
			// endpoint comparator feeds a two-unit negative error to the same multiplier, making this
			// terminal step symmetric with one unit below the exact target.
			if (direction < 0 && difference == 0)
				difference = -2;
			const auto magnitude = difference < 0 ? -difference : difference;
			const auto product = static_cast<uint32_t>(magnitude) * _rates[slot & 3];
			auto delta = static_cast<int32_t>(product >> 17);
			auto fraction = static_cast<uint8_t>(((product & 0x1ffff) + 0x1000) >> 13);
			if (fraction == 16)
			{
				++delta;
				fraction = 0;
			}
			if ((difference > 0 && threshold < fraction) ||
				(difference < 0 && fraction != 0 && threshold >= 16 - fraction))
				++delta;
			auto next = current + (direction < 0 ? -delta : delta);
			if ((direction > 0 && next > exactTarget) || (direction < 0 && next < exactTarget))
				next = exactTarget;
			m_state.iram3[slot] = ((static_cast<uint32_t>(next) & 0xffff) << 10) | low;
		}
	}

	void Dsp::setSerialInput(const SerialBus _bus, const int32_t* const _words, const size_t _count)
	{
		const auto port = static_cast<size_t>(_bus);
		if (port >= m_state.serialInput.size())
			return;
		m_state.serialInputCount[port] = static_cast<uint32_t>(std::min(_count, nSerialWords));
		for (size_t i = 0; i < m_state.serialInputCount[port]; ++i)
			m_state.serialInput[port][i] =
				static_cast<int32_t>(dspOps::signExtend(static_cast<uint32_t>(_words[i]), 24));
	}

	uint32_t Dsp::encodeBcdPacket(const int32_t _word, const uint16_t _serialFormat_3932)
	{
		const auto word = static_cast<uint32_t>(_word) & 0x00ffffff;
		if ((_serialFormat_3932 & alternateBcdPacking_3932) == 0)
		{
			const auto sign = (word >> 23) & 1;
			return (sign << 19) | (sign << 18) | ((word >> 6) & 0x0003ffff);
		}

		const auto high = (word >> 21) & 7;
		return (high << 17) | (high << 14) | ((word >> 7) & 0x00003fff);
	}

	void Dsp::runFrame(DspState& _state, const StepRequest& _request)
	{
		DspFrameContext context;
		dspOps::beginFrame(_state, context, _request);
		while (dspNaive::executeSlot(_state, m_program, context, true))
		{
		}
		dspOps::endFrame(_state, context);
	}

	void Dsp::step(const bool _executeProgram, const bool _serialInputEnabled, const uint16_t _serialAudio0Config,
				   const uint16_t _serialAudio1Config, const bool _serialOutputEnabled, const size_t _executionSlots,
				   const MixerFrame* _mixerFrame)
	{
		step({_executeProgram, _serialInputEnabled, _serialAudio0Config, _serialAudio1Config, _serialOutputEnabled,
			  _executionSlots, _mixerFrame});
	}

	void Dsp::step(const StepRequest& _request)
	{
		syncProgram(_request, true);
		runJitFrame(_request);
	}

	void Dsp::runJitFrame(const StepRequest& _request)
	{
		// The jumps form has no cycle-based deposit code: its frames must come with the deposits hoisted.
		const auto compiled = _request.executeProgram && !(m_flat.jumps && _request.mixerFrame != nullptr);
		const auto run = compiled ? jit().acquire(m_generation, m_flat, nullptr) : nullptr;
		if (run == nullptr)
		{
			runFrame(m_state, _request);
			return;
		}
		DspFrameContext context;
		dspOps::beginFrame(m_state, context, _request);
		DspJitFrame frame;
		frame.state[0] = &m_state;
		frame.params[0] = &m_params;
		frame.mixer[0] = _request.mixerFrame;
		frame.mixerBank[0] = dspOps::mixerIram(m_state).data();
		frame.processingBank[0] = dspOps::processingIram(m_state).data();
		run(&frame);
		dspOps::endFrame(m_state, context);
	}

	void Dsp::stepLinkedEngines(Dsp& _a, DspState& _aState, const StepRequest& _aRequest, Dsp& _b, DspState& _bState,
								const StepRequest& _bRequest)
	{
		DspFrameContext aContext;
		DspFrameContext bContext;
		dspOps::beginFrame(_aState, aContext, _aRequest);
		dspOps::beginFrame(_bState, bContext, _bRequest);
		const auto slots = std::max(aContext.request.executionSlots, bContext.request.executionSlots);
		constexpr auto busA = static_cast<size_t>(SerialBus::a);
		for (size_t cycle = 0; cycle < slots; ++cycle)
		{
			const auto aWords = _aState.serialOutputCount[busA];
			const auto bWords = _bState.serialOutputCount[busA];
			dspNaive::executeSlot(_aState, _a.m_program, aContext, false);
			dspNaive::executeSlot(_bState, _b.m_program, bContext, false);
			// The two programs are clocked together. A completed word changes the peer's persistent receive
			// node only after both instructions in this slot have sampled their old nodes.
			if (bContext.request.serialInputEnabled && _aState.serialOutputCount[busA] != aWords)
				_bState.serialInputNode[busA] = _aState.serialOutput[busA][aWords];
			if (aContext.request.serialInputEnabled && _bState.serialOutputCount[busA] != bWords)
				_aState.serialInputNode[busA] = _bState.serialOutput[busA][bWords];
		}
		dspOps::endFrame(_aState, aContext);
		dspOps::endFrame(_bState, bContext);
	}

	void Dsp::stepLinkedJit(Dsp& _a, const StepRequest& _aRequest, Dsp& _b, const StepRequest& _bRequest)
	{
		if (_a.m_linkPartner != &_b)
		{
			_a.m_link.reset();
			_a.m_linkPartner = &_b;
		}
		if (!_a.m_link)
		{
			_a.m_link = std::make_unique<DspJitDispatcher>();
		}
		DspJitRun run = nullptr;
		if (_aRequest.executeProgram && _bRequest.executeProgram)
			run = _a.m_link->acquire((_a.m_generation << 32) ^ _b.m_generation, _a.m_flat, &_b.m_flat);
		if (run == nullptr)
		{
			stepLinkedEngines(_a, _a.m_state, _aRequest, _b, _b.m_state, _bRequest);
			return;
		}
		DspFrameContext aContext;
		DspFrameContext bContext;
		dspOps::beginFrame(_a.m_state, aContext, _aRequest);
		dspOps::beginFrame(_b.m_state, bContext, _bRequest);
		DspJitFrame frame;
		Dsp* chips[2] = {&_a, &_b};
		const StepRequest* requests[2] = {&_aRequest, &_bRequest};
		for (size_t i = 0; i < 2; ++i)
		{
			frame.state[i] = &chips[i]->m_state;
			frame.params[i] = &chips[i]->m_params;
			frame.mixer[i] = requests[i]->mixerFrame;
			frame.mixerBank[i] = dspOps::mixerIram(chips[i]->m_state).data();
			frame.processingBank[i] = dspOps::processingIram(chips[i]->m_state).data();
		}
		run(&frame);
		dspOps::endFrame(_a.m_state, aContext);
		dspOps::endFrame(_b.m_state, bContext);
	}

	void Dsp::stepLinked(Dsp& _a, const StepRequest& _aRequest, Dsp& _b, const StepRequest& _bRequest)
	{
		_a.syncProgram(_aRequest, false, true);
		_b.syncProgram(_bRequest, false, true);
		stepLinkedJit(_a, _aRequest, _b, _bRequest);
	}
} // namespace xpLib
