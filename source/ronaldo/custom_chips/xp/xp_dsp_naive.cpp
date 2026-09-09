#include "xp_dsp.h"
#include "xp_dsp_ops.h"

namespace xpLib::dspNaive
{
	namespace
	{
		using namespace dspOps;

		struct DecodedInstruction
		{
			uint8_t ioCtrl = 0;
			uint16_t eram = 0;
			uint8_t store = 0;
			uint8_t memaddr = 0;
			uint8_t op = 0;
			uint16_t cram = 0;
		};

		DecodedInstruction decode(const DspProgram& _program, const size_t _pc)
		{
			const auto word = _program.pram[_pc] & 0x0fffffff;
			return {
				static_cast<uint8_t>((word >> 25) & 7), static_cast<uint16_t>((word >> 16) & 0x01ff),
				static_cast<uint8_t>((word >> 14) & 3), static_cast<uint8_t>((word >> 6) & 0xff),
				static_cast<uint8_t>(word & 0x3f),		_program.cram[_pc],
			};
		}

		// A normal ERAM command spans two consecutive PRAM words: the first arms it and captures the write
		// data, the second carries the low offset and performs the access. Returns true when this slot
		// consumed a second word, which occupies the data port.
		bool executeEram(DspState& _state, const uint16_t _eram)
		{
			if (_state.eramPrefixPending)
			{
				const auto offset = (static_cast<uint32_t>(_state.eramOffsetHigh) << 9) | (_eram & 0x01ff);
				const auto address = (_state.eramPos + offset) & 0xffff;
				if (_state.eramPendingWrite)
					_state.eram[address] = encode24(_state.eramPendingWriteValue);
				else
					queueEramRead(_state, static_cast<int32_t>(signExtend(_state.eram[address], 24)), 1);
				_state.eramPrefixPending = 0;
				return true;
			}
			if ((_eram & 0x0180) != 0)
			{
				_state.eramPrefixPending = 1;
				_state.eramPendingWrite = (_eram & 0x0100) != 0;
				_state.eramOffsetHigh = static_cast<uint8_t>(_eram & 0x7f);
				// Bit 7 selects the accumulator; bit 8 alone captures the IRAM read latch.
				_state.eramPendingWriteValue = (_eram & 0x0080) != 0 ? _state.accumulator : _state.iramReadLatch;
				return false;
			}
			return false;
		}

		int64_t executePrimary(const DspState& _state, const uint8_t _function, const uint8_t _mode,
							   const uint16_t _cram, const int64_t _accumulator)
		{
			switch (_function)
			{
			case 1:
				return _accumulator;
			case 2:
				return _accumulator + _state.iramReadLatch;
			case 3:
				return _accumulator + _state.multiplyResultLatch;
			case 4:
				return _state.iramReadLatch;
			case 5:
				return _state.multiplyResultLatch;
			case 6:
				return -_accumulator;
			case 7:
				return _state.iramReadLatch - _accumulator;
			case 8:
				return _state.multiplyResultLatch - _accumulator;
			case 9:
				return _state.iramReadLatch + _state.multiplyResultLatch;
			case 10:
				return std::min(_accumulator, _state.iramReadLatch);
			case 11:
				return std::max(_accumulator, _state.iramReadLatch);
			case 12:
				if (_mode == 2)
					return _accumulator + (_state.multiplyResultLatch >> 13);
				return _mode == 3 ? _state.multiplyResultLatch >> 13 : _accumulator;
			case 13:
				{
					if (_mode == 0)
						return _accumulator;
					constexpr auto accumulatorMask = (int64_t{1} << 29) - 1;
					// Bit 15 shifts the unsigned 15-bit field into the guard bits; without it the field is
					// sign-extended to the 24-bit datapath. XP3-verified: they differ when bit 14 is set too.
					auto immediate = (_cram & 0x8000) != 0 ? static_cast<int64_t>(_cram & 0x7fff) << 13
														   : signExtend(_cram & 0x7fff, 15) & 0x00ffffff;
					immediate &= accumulatorMask;
					const auto accumulator = _accumulator & accumulatorMask;
					if (_mode == 1)
						return accumulator & immediate;
					return _mode == 2 ? accumulator | immediate : accumulator ^ immediate;
				}
			case 14:
				{
					const auto immediate = decodeCramImmediate(_cram);
					if (_mode == 0)
						return std::min(_accumulator, immediate);
					const auto sameSign = (_accumulator < 0) == (immediate < 0);
					if (_mode == 2)
						return sameSign ? std::min(_accumulator, immediate) : std::max(_accumulator, immediate);
					if (_mode == 3)
						return sameSign ? std::max(_accumulator, immediate) : std::min(_accumulator, immediate);
					return std::max(_accumulator, immediate);
				}
			case 15:
				{
					const auto immediate = decodeCramImmediate(_cram);
					switch (_mode)
					{
					case 0:
						return _accumulator + immediate;
					case 1:
						return _state.iramReadLatch + immediate;
					case 2:
						return _state.multiplyResultLatch + immediate;
					default:
						return -_accumulator + immediate;
					}
				}
			default:
				return _accumulator;
			}
		}

		int64_t executeParallel(const DspState& _state, const uint16_t _cram, const int64_t _accumulator,
								bool& _multiplyIssued, int64_t& _nextMultiply, bool& _nextNegativeFraction,
								int64_t& _multiplyInput)
		{
			if ((_cram & 0x0200) != 0)
			{
				const auto inputSelect = static_cast<uint8_t>((_cram >> 4) & 3);
				const std::array<int64_t, 4> inputs = {_state.multiplyFeedbackLatch, saturate(_accumulator, 24),
													   _state.iramReadLatch, _state.eramReadLatch};
				_multiplyInput = inputs[inputSelect];
				const auto saturatedAccumulator = saturate(_accumulator, 24);
				const auto factorSelect = static_cast<uint8_t>((_cram >> 6) & 3);
				int64_t factor = 0;
				switch (factorSelect)
				{
				case 0:
					factor = (saturatedAccumulator & 0x0fff) << 3;
					break;
				case 1:
					factor = (saturatedAccumulator & 0x7fffff) >> 8;
					break;
				case 2:
					factor = saturatedAccumulator >> 8;
					break;
				case 3:
					factor = signExtend(_state.iram3ParameterLatch, 16);
					break;
				}
				if ((_cram & 0x0100) != 0)
				{
					factor = 0x7fff - factor;
					if (factorSelect >= 2)
						factor -= 0x8000;
				}
				static constexpr std::array<unsigned, 4> shifts = {0, 1, 2, 4};
				const auto numerator = (_multiplyInput * factor) << shifts[_cram >> 14];
				_nextMultiply = saturate(numerator / 32768, 29);
				_nextNegativeFraction = multiplyNegativeFraction(numerator, 15);
				_multiplyIssued = true;
			}

			int64_t result = _accumulator;
			switch (_cram & 0x0f)
			{
			case 0:
				result = _accumulator + _state.multiplyResultLatch + _state.iramReadLatch;
				break;
			case 2:
				result = _accumulator + _state.iramReadLatch;
				break;
			case 3:
				result = _accumulator + _state.multiplyResultLatch;
				break;
			case 4:
				result = _state.iramReadLatch;
				break;
			case 5:
				result = _state.multiplyResultLatch;
				break;
			case 6:
				result = -_accumulator;
				break;
			case 7:
				result = _state.iramReadLatch - _accumulator;
				break;
			case 8:
				result = _state.multiplyResultLatch - _accumulator;
				break;
			case 9:
				result = _state.iramReadLatch + _state.multiplyResultLatch;
				break;
			case 10:
				result = std::min(_accumulator, _state.iramReadLatch);
				break;
			case 11:
				result = std::max(_accumulator, _state.iramReadLatch);
				break;
			case 12:
				result = _state.iramReadLatch + _state.multiplyResultLatch - _accumulator;
				break;
			// D-F also consume the discarded negative fraction of the preceding multiply (XP3-verified).
			case 13:
				result = _accumulator + _state.multiplyResultLatch - _state.iramReadLatch -
					(_state.multiplyNegativeFraction ? 1 : 0);
				break;
			case 14:
				result = _state.multiplyResultLatch - _state.iramReadLatch - _accumulator -
					(_state.multiplyNegativeFraction ? 2 : 0);
				break;
			case 15:
				result = _state.multiplyResultLatch - _state.iramReadLatch - (_state.multiplyNegativeFraction ? 2 : 0);
				break;
			default:
				break;
			}

			switch ((_cram >> 11) & 7)
			{
			case 1:
				if (result < 0)
					result = -result;
				break;
			case 2:
				result = signExtend(result, 24);
				break;
			case 3:
				result = (result << 1) | (((result >> 23) ^ (result >> 6) ^ (result >> 1)) & 1);
				break;
			case 4:
				result &= 0x00ffffff;
				if ((((result >> 23) ^ (result >> 22)) & 1) != 0)
					result ^= 0x007fffff;
				result = signExtend(result, 24);
				break;
			case 5:
				result = signExtend(result, 24);
				if (result < 0)
					result = ~result;
				break;
			default:
				break;
			}
			if ((_cram & 0x0400) != 0)
				result = signExtend(result, 24);
			return result;
		}

		bool branchTaken(const DspState& _state, const uint8_t _condition)
		{
			switch (_condition)
			{
			case 0:
				return _state.accumulator == 0;
			case 1:
				return _state.accumulator != 0;
			case 3:
			case 5:
			case 13:
			case 14:
				return true;
			case 6:
			case 8:
				return _state.accumulator >= 0;
			case 7:
			case 9:
				return _state.accumulator < 0;
			case 10:
				return _state.accumulator > 0;
			case 11:
				return _state.accumulator <= 0;
			default:
				return false;
			}
		}
	} // namespace

	bool executeSlot(DspState& _state, const DspProgram& _program, DspFrameContext& _context,
					 const bool _consumeStagedBusA)
	{
		if (_context.cycle >= _context.request.executionSlots)
			return false;
		if (_context.request.mixerFrame)
			stepMixerCycle(_state, *_context.request.mixerFrame, _context.cycle);
		if (!_context.request.executeProgram || _context.pc >= dsp::nProgramSlots)
		{
			++_context.cycle;
			return true;
		}

		const auto pc = _context.pc;
		const auto instruction = decode(_program, pc);
		const auto partitionCount = iram3PartitionCount(_context.request.serialAudio1Config);
		const auto parameterReadBoundary = iram3ParameterReadBoundary(partitionCount);
		advanceEramReads(_state);
		const auto eramPortBusy = executeEram(_state, instruction.eram);

		int64_t iramReadValue = 0;
		bool loadsIramReadLatch = false;
		const auto selectsIram3 = instruction.memaddr >= 0xc0;
		if (instruction.store == 1)
		{
			iramReadValue = readIram(_state, instruction.memaddr);
			loadsIramReadLatch = !(selectsIram3 && (instruction.memaddr & 0x3f) >= parameterReadBoundary);
		}
		else if (instruction.store == 2)
			writeIram(_state, instruction.memaddr, _state.eramReadLatch);
		else if (instruction.store == 3)
			writeIram(_state, instruction.memaddr, _state.accumulator);
		executeIo(_state, instruction.ioCtrl, _context.request.serialInputEnabled,
				  _context.request.serialInputEnabled && _context.request.serialOutputEnabled,
				  _context.request.serialAudio0Config, _context.request.serialAudio1Config);

		const auto mode = static_cast<uint8_t>(instruction.op >> 4);
		const auto function = static_cast<uint8_t>(instruction.op & 0x0f);
		bool multiplyIssued = false;
		int64_t nextMultiply = 0;
		bool nextNegativeFraction = false;
		int64_t multiplyInput = 0;
		if (function >= 1 && function <= 12)
		{
			const std::array<int64_t, 4> inputs = {_state.multiplyFeedbackLatch, saturate(_state.accumulator, 24),
												   _state.iramReadLatch, _state.eramReadLatch};
			multiplyInput = inputs[mode];
			auto coefficientInput = multiplyInput;
			if (function == 12)
			{
				// Modes 0/1 select SDIA/SDIB. SC-8850 XP0 receives the LSP return on SDIB.
				if (mode < 2)
				{
					const auto port = mode == 0 ? DspSerialBus::a : DspSerialBus::b;
					const auto index = static_cast<size_t>(port);
					coefficientInput = _state.serialInputNode[index];
				}
				else
					coefficientInput = _state.multiplyFeedbackLatch;
			}
			const auto numerator = coefficientInput * cramCoefficient(instruction.cram);
			nextMultiply = saturate(numerator / 8192, 29);
			nextNegativeFraction = multiplyNegativeFraction(numerator, 13);
			multiplyIssued = true;
		}

		auto nextParameterLatch = _state.iram3ParameterLatch;
		if (instruction.store == 1 && selectsIram3 && (instruction.memaddr & 0x3f) >= parameterReadBoundary)
			nextParameterLatch = static_cast<uint16_t>((_state.iram3[instruction.memaddr & 0x3f] >> 10) & 0xffff);

		auto nextAccumulator = _state.accumulator;
		bool branch = false;
		size_t branchTarget = 0;
		if (function != 0)
			nextAccumulator = executePrimary(_state, function, mode, instruction.cram, _state.accumulator);
		else if (instruction.op == 0x30)
			nextAccumulator = executeParallel(_state, instruction.cram, _state.accumulator, multiplyIssued,
											  nextMultiply, nextNegativeFraction, multiplyInput);
		else if (instruction.op == 0x20 && !eramPortBusy)
		{
			_state.eramIndexedOffset = static_cast<uint16_t>((_state.accumulator >> 12) & 0xffff);
			const auto address = (_state.eramPos + _state.eramIndexedOffset) & 0xffff;
			queueEramRead(_state, static_cast<int32_t>(signExtend(_state.eram[address], 24)), 2);
		}
		else if (instruction.op == 0x10)
		{
			const auto branchControl = static_cast<uint8_t>((instruction.cram >> 8) & 0x3f);
			branch = branchTaken(_state, branchControl >> 2);
			branchTarget = (branchControl & 2) != 0 ? pc + 1 + (instruction.cram & 0xff) : instruction.cram & 0xff;
		}

		_state.accumulator = signExtend(nextAccumulator, 29);
		if (instruction.store == 1)
			_state.iram3ParameterLatch = nextParameterLatch;
		if (loadsIramReadLatch)
			_state.iramReadLatch = iramReadValue;
		if (multiplyIssued)
		{
			_state.multiplyResultLatch = nextMultiply;
			_state.multiplyNegativeFraction = nextNegativeFraction;
			_state.multiplyFeedbackLatch = multiplyInput;
		}
		// Serial inputs are persistent receive nodes, not queues consumed by function C. Bus A commits a word
		// at each ioCtrl=1 boundary. Bus B commits at the first ioCtrl=2 event in each three-event group
		// (the SDOB word). A function-C operation sharing that instruction samples the word which was
		// complete before the boundary.
		if (_context.request.serialInputEnabled && _consumeStagedBusA && instruction.ioCtrl == 1)
			consumeSerialInput(_state, DspSerialBus::a);
		else if (_context.request.serialInputEnabled && instruction.ioCtrl == 2 && _state.dacPortPosition == 1)
			consumeSerialInput(_state, DspSerialBus::b);
		_context.pc = branch ? branchTarget : pc + 1;
		++_context.cycle;
		return true;
	}
} // namespace xpLib::dspNaive
