#include "xp_dsp_program.h"

#include "xp_dsp_ops.h"

#include <algorithm>

namespace xpLib
{
	namespace
	{
		constexpr std::array<unsigned, 4> g_coefficientShifts = {0, 1, 2, 4};

		constexpr std::array<DspMultiplyInput, 4> g_multiplyInputs = {
			DspMultiplyInput::feedbackLatch, DspMultiplyInput::accumulatorSat24, DspMultiplyInput::iramReadLatch,
			DspMultiplyInput::eramReadLatch};

		constexpr std::array<DspAlu, 16> g_parallelAlu = {
			DspAlu::accPlusMulPlusIram,  DspAlu::hold,               DspAlu::accPlusIram,          DspAlu::accPlusMul,
			DspAlu::iram,                DspAlu::mul,                DspAlu::negAcc,               DspAlu::iramMinusAcc,
			DspAlu::mulMinusAcc,         DspAlu::iramPlusMul,        DspAlu::minAccIram,           DspAlu::maxAccIram,
			DspAlu::iramPlusMulMinusAcc, DspAlu::accPlusMulMinusIram, DspAlu::mulMinusIramMinusAcc, DspAlu::mulMinusIram};

		constexpr std::array<DspTransform, 8> g_transforms = {
			DspTransform::none, DspTransform::absolute,   DspTransform::sext24,
			DspTransform::lfsr, DspTransform::foldMirror, DspTransform::onesComplementNegative,
			DspTransform::none, DspTransform::none};

		constexpr std::array<DspBranch, 16> g_branches = {
			DspBranch::eq0,   DspBranch::ne0,    DspBranch::never,  DspBranch::always, DspBranch::never, DspBranch::always,
			DspBranch::ge0,   DspBranch::lt0,    DspBranch::ge0,    DspBranch::lt0,    DspBranch::gt0,   DspBranch::le0,
			DspBranch::never, DspBranch::always, DspBranch::always, DspBranch::never};

		DspAlu primaryAlu(const uint8_t _function, const uint8_t _mode)
		{
			switch (_function)
			{
			case 2:
				return DspAlu::accPlusIram;
			case 3:
				return DspAlu::accPlusMul;
			case 4:
				return DspAlu::iram;
			case 5:
				return DspAlu::mul;
			case 6:
				return DspAlu::negAcc;
			case 7:
				return DspAlu::iramMinusAcc;
			case 8:
				return DspAlu::mulMinusAcc;
			case 9:
				return DspAlu::iramPlusMul;
			case 10:
				return DspAlu::minAccIram;
			case 11:
				return DspAlu::maxAccIram;
			case 12:
				return _mode == 2 ? DspAlu::accPlusMulShr13 : _mode == 3 ? DspAlu::mulShr13 : DspAlu::hold;
			case 13:
				{
					static constexpr std::array<DspAlu, 4> logical = {DspAlu::hold, DspAlu::andImm, DspAlu::orImm,
																	  DspAlu::xorImm};
					return logical[_mode];
				}
			case 14:
				{
					static constexpr std::array<DspAlu, 4> compare = {DspAlu::minImm, DspAlu::maxImm,
																	  DspAlu::sameSignMinElseMax,
																	  DspAlu::sameSignMaxElseMin};
					return compare[_mode];
				}
			case 15:
				{
					static constexpr std::array<DspAlu, 4> add = {DspAlu::accPlusImm, DspAlu::iramPlusImm,
																  DspAlu::mulPlusImm, DspAlu::negAccPlusImm};
					return add[_mode];
				}
			default:
				return DspAlu::hold;
			}
		}

	struct DspSlotDecode
	{
		uint8_t ioCtrl = 0;
		uint8_t memaddr = 0;
		DspIramBank bank = DspIramBank::mixer;
		uint8_t index = 0; // cell within the bank
		DspTransfer transfer = DspTransfer::none;
		DspEramArm eramArm = DspEramArm::none;
		DspSlotOp op = DspSlotOp::none;
		DspAlu alu = DspAlu::hold;
		DspMultiply multiply = DspMultiply::none;
		DspMultiplyInput multiplicand = DspMultiplyInput::feedbackLatch;
		DspMultiplyInput feedbackSource = DspMultiplyInput::feedbackLatch;
		DspMultiplyFactor factor = DspMultiplyFactor::accLow12Shl3;
		uint8_t factorComplement = 0;
		uint8_t gainShift = 0;
		DspTransform transform = DspTransform::none;
		uint8_t sext24Result = 0;
		DspBranch branch = DspBranch::never;
		uint16_t branchTarget = 0; // may be >= nProgramSlots: the rest of the frame idles
	};

	void dspDecodeIramAddress(const uint8_t _memaddr, DspIramBank& _bank, uint8_t& _index)
	{
		if (_memaddr < 0x80)
		{
			_bank = (_memaddr & 0x40) != 0 ? DspIramBank::processing : DspIramBank::mixer;
			_index = static_cast<uint8_t>(_memaddr & 0x3f);
			return;
		}
		if (_memaddr < 0xc0)
		{
			_bank = (_memaddr & 0x20) != 0 ? DspIramBank::direct2 : DspIramBank::direct1;
			_index = static_cast<uint8_t>(0x20 + (_memaddr & 0x1f));
			return;
		}
		_bank = DspIramBank::iram3;
		_index = static_cast<uint8_t>(_memaddr & 0x3f);
	}

	void dspDecodeSlot(const uint32_t _pram, const uint16_t _cram, const size_t _pc,
					   const uint8_t _iram3ParameterReadBoundary, DspSlotDecode& _slot)
	{
		_slot = DspSlotDecode{};

		const auto word = _pram & 0x0fffffff;
		_slot.ioCtrl = static_cast<uint8_t>((word >> 25) & 7);
		const auto eram = static_cast<uint16_t>((word >> 16) & 0x01ff);
		const auto store = static_cast<uint8_t>((word >> 14) & 3);
		_slot.memaddr = static_cast<uint8_t>((word >> 6) & 0xff);
		dspDecodeIramAddress(_slot.memaddr, _slot.bank, _slot.index);
		const auto op = static_cast<uint8_t>(word & 0x3f);
		const auto mode = static_cast<uint8_t>(op >> 4);
		const auto function = static_cast<uint8_t>(op & 0x0f);

		if ((eram & 0x0100) != 0)
			_slot.eramArm = (eram & 0x0080) != 0 ? DspEramArm::writeAccumulator : DspEramArm::writeIramLatch;
		else if ((eram & 0x0080) != 0)
			_slot.eramArm = DspEramArm::read;

		switch (store)
		{
		case 1:
			_slot.transfer = _slot.bank == DspIramBank::iram3 && _slot.index >= _iram3ParameterReadBoundary
				? DspTransfer::readParameter
				: DspTransfer::readState;
			break;
		case 2:
			_slot.transfer = DspTransfer::writeEramReadLatch;
			break;
		case 3:
			_slot.transfer = DspTransfer::writeAccumulator;
			break;
		default:
			break;
		}

		if (function != 0)
		{
			_slot.op = DspSlotOp::primary;
			_slot.alu = primaryAlu(function, mode);
			if (function <= 12)
			{
				_slot.multiply = DspMultiply::coefficient;
				_slot.feedbackSource = g_multiplyInputs[mode];
				// Function C multiplies a serial receive node (or the feedback latch) but still records the
				// mode-selected operand in the feedback latch.
				_slot.multiplicand = function == 12
					? (mode == 0 ? DspMultiplyInput::serialNodeA
								 : mode == 1 ? DspMultiplyInput::serialNodeB : DspMultiplyInput::feedbackLatch)
					: g_multiplyInputs[mode];
			}
			return;
		}

		switch (op)
		{
		case 0x10:
			{
				_slot.op = DspSlotOp::branch;
				const auto control = static_cast<uint8_t>((_cram >> 8) & 0x3f);
				_slot.branch = g_branches[control >> 2];
				const auto target = static_cast<uint16_t>(_cram & 0xff);
				_slot.branchTarget = (control & 2) != 0 ? static_cast<uint16_t>(_pc + 1 + target) : target;
				break;
			}
		case 0x20:
			_slot.op = DspSlotOp::indexedEramRead;
			break;
		case 0x30:
			_slot.op = DspSlotOp::parallel;
			_slot.alu = g_parallelAlu[_cram & 0x0f];
			_slot.transform = g_transforms[(_cram >> 11) & 7];
			_slot.sext24Result = static_cast<uint8_t>((_cram >> 10) & 1);
			if ((_cram & 0x0200) != 0)
			{
				_slot.multiply = DspMultiply::factor;
				_slot.multiplicand = _slot.feedbackSource = g_multiplyInputs[(_cram >> 4) & 3];
				_slot.factor = static_cast<DspMultiplyFactor>((_cram >> 6) & 3);
				_slot.factorComplement = static_cast<uint8_t>((_cram >> 8) & 1);
				_slot.gainShift = static_cast<uint8_t>(g_coefficientShifts[_cram >> 14]);
			}
			break;
		default:
			break;
		}
	}
	} // namespace

	int32_t dspDecodeParam(const uint32_t _pram, const uint16_t _cram)
	{
		const auto function = static_cast<uint8_t>(_pram & 0x0f);
		if (function == 0)
			return 0;
		if (function <= 12)
			return static_cast<int32_t>(dspOps::signExtend(_cram & 0x3fff, 14) << g_coefficientShifts[_cram >> 14]);
		if (function == 13)
		{
			// Bit 15 shifts the unsigned 15-bit field into the guard bits; without it the field is sign-extended
			// to the 24-bit datapath. XP3-verified: the two differ when bit 14 is set as well.
			const auto immediate = (_cram & 0x8000) != 0 ? static_cast<int64_t>(_cram & 0x7fff) << 13
														 : dspOps::signExtend(_cram & 0x7fff, 15) & 0x00ffffff;
			return static_cast<int32_t>(immediate & ((int64_t{1} << 29) - 1));
		}
		return static_cast<int32_t>(dspOps::decodeCramImmediate(_cram));
	}

	bool dspCramWriteIsStructural(const uint32_t _pram)
	{
		const auto op = static_cast<uint8_t>(_pram & 0x3f);
		return (op & 0x0f) == 0 && (op == 0x10 || op == 0x30);
	}

	bool dspPramWriteIsStructural(const uint32_t _oldPram, const uint32_t _newPram)
	{
		constexpr uint32_t eramMask = 0x01ff0000;
		constexpr uint32_t commandKindMask = 0x01800000; // bits 8:7 of the eram field
		if (((_oldPram ^ _newPram) & ~eramMask & 0x0fffffff) != 0)
			return true;
		return ((_oldPram ^ _newPram) & commandKindMask) != 0;
	}

	void dspRefreshEramOffsets(const DspProgram& _program, DspParams& _params, const size_t _slot)
	{
		for (size_t slot = _slot; slot <= _slot + 1 && slot < dsp::nProgramSlots; ++slot)
		{
			const auto eram = static_cast<uint16_t>((_program.pram[slot] >> 16) & 0x01ff);
			_params.eramOffsetHigh[slot] = static_cast<uint8_t>(eram & 0x7f);
			_params.eramOffsetLow[slot] = eram;
			const auto high = slot == 0 ? 0u : (_program.pram[slot - 1] >> 16) & 0x7f;
			_params.eramOffset[slot] = static_cast<uint16_t>((high << 9) | eram);
		}
	}

	void dspRefreshParams(const DspProgram& _program, DspParams& _params)
	{
		for (size_t slot = 0; slot < dsp::nProgramSlots; ++slot)
			_params.param[slot] = dspDecodeParam(_program.pram[slot], _program.cram[slot]);
		for (size_t slot = 0; slot < dsp::nProgramSlots; slot += 2)
			dspRefreshEramOffsets(_program, _params, slot);
	}
} // namespace xpLib

#include "xp_dsp_ops.h"

namespace xpLib
{
	DspFrameConfig dspFrameConfig(const DspStepRequest& _request, const bool _consumeStagedBusA, const bool _linked)
	{
		DspFrameConfig config;
		config.linked = _linked;
		config.serialInputEnabled = _request.serialInputEnabled;
		config.serialOutputEnabled = _request.serialInputEnabled && _request.serialOutputEnabled;
		config.consumeStagedBusA = _consumeStagedBusA;
		config.bcdEnabled = static_cast<uint8_t>((((_request.serialAudio0Config >> 8) & 0xc0) != 0 ? 1 : 0) |
												 ((_request.serialAudio1Config & 0xc0) != 0 ? 6 : 0));
		config.iram3ParameterReadBoundary =
			dspOps::iram3ParameterReadBoundary(dspOps::iram3PartitionCount(_request.serialAudio1Config));
		config.executionSlots = static_cast<uint16_t>(std::min(_request.executionSlots, dsp::nExecutionSlots));
		return config;
	}

	namespace
	{
		struct Lowering
		{
			const DspFrameConfig& config;
			FlatProgram& flat;
			std::array<DspSlotDecode, dsp::nProgramSlots> slots{};

			// Static IO schedule.
			uint32_t wordPosition = 0;
			uint32_t ordinal = 0;
			uint32_t countA = 0;
			std::array<uint32_t, 3> countBcd{};

			// Static ERAM pairing and read queue.
			DspEramArm pendingKind = DspEramArm::none;
			struct Entry
			{
				int countdown = -1;
				bool indexed = false;
				uint16_t slot = 0;
			};
			std::array<Entry, 2> queue{};

			void emit(const FlatOpKind _kind, const uint8_t _a = 0, const uint8_t _b = 0, const uint8_t _c = 0,
					  const uint16_t _index = 0)
			{
				flat.ops.push_back({_kind, _a, _b, _c, _index});
			}

			void noteMixerCell(const DspSlotDecode& _slot)
			{
				if (_slot.transfer == DspTransfer::none)
					return;
				const auto bit = uint64_t{1} << _slot.index;
				switch (_slot.bank)
				{
				case DspIramBank::mixer:
					flat.mixerCells[0] |= bit;
					flat.mixerCells[1] |= bit;
					break;
				case DspIramBank::direct1: // IRAM1 is the mixer bank in phase 0
					flat.mixerCells[0] |= bit;
					break;
				case DspIramBank::direct2:
					flat.mixerCells[1] |= bit;
					break;
				default:
					break;
				}
			}

			static bool slotHasEvent(const DspSlotDecode& _slot)
			{
				return _slot.ioCtrl == 1 || _slot.ioCtrl == 2 || _slot.eramArm != DspEramArm::none ||
					_slot.op == DspSlotOp::indexedEramRead;
			}

			// The jumps form: every branch is a forward skip inside the program, its skipped slots and the
			// window in which the frame can end carry no serial event and no ERAM command, so the static
			// schedules hold on every path. Returns the last slot any path can reach within the budget.
			bool jumpsEligible(uint16_t& _lastSlot) const
			{
				const auto slotCount = config.executionSlots;
				size_t skipMax = 0;
				bool any = false;
				for (size_t pc = 0; pc < dsp::nProgramSlots; ++pc)
				{
					const auto& slot = slots[pc];
					if (slot.op != DspSlotOp::branch || slot.branch == DspBranch::never)
						continue;
					any = true;
					const auto target = slot.branchTarget;
					if (target <= pc || target >= dsp::nProgramSlots)
						return false;
					for (size_t skipped = pc + 1; skipped < target; ++skipped)
						if (slotHasEvent(slots[skipped]))
							return false;
					skipMax += target - pc - 1;
				}
				if (!any || slotCount < 2)
					return false;
				_lastSlot = static_cast<uint16_t>(std::min<size_t>(dsp::nProgramSlots - 1, slotCount - 1 + skipMax));
				for (size_t pc = slotCount - 2; pc <= _lastSlot; ++pc)
					if (slotHasEvent(slots[pc]))
						return false;
				return true;
			}

			int allocateEntry()
			{
				for (int e = 0; e < 2; ++e)
					if (queue[e].countdown < 0)
						return e;
				return 1; // unreachable: at most two reads are ever in flight
			}

			// The naive engine lands reads at the top of every slot, iterating the queue in entry order.
			void landStatic()
			{
				for (int e = 0; e < 2; ++e)
				{
					auto& entry = queue[e];
					if (entry.countdown < 0 || --entry.countdown != 0)
						continue;
					if (entry.indexed)
						emit(FlatOpKind::eramLandIndexed, static_cast<uint8_t>(e));
					else
						emit(FlatOpKind::eramLand, static_cast<uint8_t>(e), 0, 0, entry.slot);
					entry.countdown = -1;
				}
			}

			void emitIo(const DspSlotDecode& _slot, const bool _dynamic)
			{
				if (_slot.ioCtrl >= 4)
				{
					emit(FlatOpKind::pins, static_cast<uint8_t>(_slot.ioCtrl & 3));
					return;
				}
				if (!config.serialInputEnabled || (_slot.ioCtrl != 1 && _slot.ioCtrl != 2))
					return;
				if (_dynamic)
				{
					if (_slot.ioCtrl == 1)
						emit(FlatOpKind::emitADynamic, config.serialOutputEnabled);
					else
						emit(FlatOpKind::emitBcdDynamic,
							 static_cast<uint8_t>(config.serialOutputEnabled ? config.bcdEnabled : 0));
					return;
				}
				const auto word = static_cast<uint8_t>(wordPosition & (dsp::nIramSlots - 1));
				if (_slot.ioCtrl == 1)
				{
					if (config.serialOutputEnabled && countA < dsp::nSerialWords)
						emit(FlatOpKind::emitA, static_cast<uint8_t>(countA++), word);
				}
				else
				{
					const auto port = static_cast<uint8_t>(ordinal % 3);
					const auto enabled = config.serialOutputEnabled && ((config.bcdEnabled >> port) & 1) != 0;
					uint8_t busIndex = 0xff;
					if (enabled && countBcd[port] < dsp::nSerialWords)
						busIndex = static_cast<uint8_t>(countBcd[port]++);

					if (busIndex != 0xff)
						emit(FlatOpKind::emitBcd, port, word, busIndex);
					++ordinal;
				}
				++wordPosition;
			}

			void emitConsume(const DspSlotDecode& _slot, const bool _dynamic)
			{
				if (!config.serialInputEnabled)
					return;
				if (_slot.ioCtrl == 1 && config.consumeStagedBusA)
					emit(FlatOpKind::consumeA);
				else if (_slot.ioCtrl == 2)
				{
					if (_dynamic)
						emit(FlatOpKind::consumeBDynamic);
					else if ((ordinal - 1) % 3 == 0) // ordinal was advanced by emitIo: this event was the SDOB word
						emit(FlatOpKind::consumeB);
				}
			}

			void emitDatapath(const DspSlotDecode& _slot, const uint16_t _pc)
			{
				if (_slot.multiply != DspMultiply::none)
					emit(FlatOpKind::mulCapture, static_cast<uint8_t>(_slot.multiplicand),
						 static_cast<uint8_t>(_slot.feedbackSource));
				if (_slot.multiply == DspMultiply::factor)
					emit(FlatOpKind::factorCapture, static_cast<uint8_t>(_slot.factor), _slot.factorComplement);
				switch (_slot.op)
				{
				case DspSlotOp::primary:
					if (_slot.alu != DspAlu::hold)
						emit(FlatOpKind::alu, static_cast<uint8_t>(_slot.alu), 0, 0, _pc);
					break;
				case DspSlotOp::parallel:
					emit(FlatOpKind::parallel, static_cast<uint8_t>(_slot.alu), static_cast<uint8_t>(_slot.transform),
						 _slot.sext24Result);
					break;
				case DspSlotOp::branch:
					if (_slot.branch != DspBranch::never)
						emit(FlatOpKind::branch, static_cast<uint8_t>(_slot.branch), 0, 0, _slot.branchTarget);
					break;
				default:
					break;
				}
				if (_slot.multiply == DspMultiply::coefficient)
					emit(FlatOpKind::mulCoefficient, 0, 0, 0, _pc);
				else if (_slot.multiply == DspMultiply::factor)
					emit(FlatOpKind::mulFactor, _slot.gainShift);
			}

			void emitTransferWrites(const DspSlotDecode& _slot)
			{
				if (_slot.transfer == DspTransfer::writeAccumulator)
					emit(FlatOpKind::iramWriteAcc, static_cast<uint8_t>(_slot.bank), _slot.index);
				else if (_slot.transfer == DspTransfer::writeEramReadLatch)
					emit(FlatOpKind::iramWriteLatch, static_cast<uint8_t>(_slot.bank), _slot.index);
			}

			void emitTransferReads(const DspSlotDecode& _slot)
			{
				if (_slot.transfer == DspTransfer::readState)
					emit(FlatOpKind::iramRead, static_cast<uint8_t>(_slot.bank), _slot.index);
				else if (_slot.transfer == DspTransfer::readParameter)
					emit(FlatOpKind::iramReadParam, 0, _slot.index);
			}

			void lowerDynamic()
			{
				flat.ops.clear();
				flat.dynamic = true;
				flat.jumps = false;
				flat.slotsLowered = dsp::nProgramSlots;
				flat.staticStart = dsp::nProgramSlots;
				flat.mixerCells = {};
				flat.carry = FlatCarry{};
				for (uint16_t pc = 0; pc < dsp::nProgramSlots; ++pc)
				{
					const auto& slot = slots[pc];
					noteMixerCell(slot);
					flat.slotToOp[pc] = static_cast<uint16_t>(flat.ops.size());
					emit(FlatOpKind::eramAdvance);
					emit(FlatOpKind::eramGeneric, static_cast<uint8_t>(slot.eramArm), 0, 0, pc);
					emitTransferWrites(slot);
					emitIo(slot, true);
					if (slot.op == DspSlotOp::indexedEramRead)
						emit(FlatOpKind::eramIndexedGeneric);
					emitDatapath(slot, pc);
					emitTransferReads(slot);
					emitConsume(slot, true);
				}
				flat.slotToOp[dsp::nProgramSlots] = static_cast<uint16_t>(flat.ops.size());
			}

			// Static form; with _jumps the branches stay in and the slots through _lastSlot are lowered. Returns
			// false when a branch slot's ERAM state is not static after all, which sends the program to the
			// dynamic form.
			bool lowerStatic(const bool _jumps, const uint16_t _lastSlot)
			{
				const auto slotCount = config.executionSlots;
				const auto emitCount = static_cast<uint16_t>(_jumps ? _lastSlot + 1 : slotCount);
				flat.jumps = _jumps;
				flat.slotsLowered = emitCount;
				// The two-word pairing resynchronises after the first slot without arm bits, whatever the
				// previous frame left pending. Reads queued by a slot that may issue an indexed read (which
				// depends on the runtime port state) keep the queue in runtime form for one more slot.
				uint16_t resync = 0;
				while (resync < slotCount && slots[resync].eramArm != DspEramArm::none)
					++resync;
				uint16_t staticStart = static_cast<uint16_t>(std::min<size_t>(resync + 1, slotCount));
				while (staticStart < slotCount && slots[staticStart - 1].op == DspSlotOp::indexedEramRead)
					++staticStart;
				flat.staticStart = staticStart;

				for (uint16_t pc = 0; pc < emitCount; ++pc)
				{
					const auto& slot = slots[pc];
					noteMixerCell(slot);
					flat.slotToOp[pc] = static_cast<uint16_t>(flat.ops.size());
					const bool generic = pc < staticStart;

					// Landings (reads still in flight from the previous frame land in slots 0 and 1), then this
					// slot's ERAM command.
					if (pc <= staticStart)
						emit(FlatOpKind::eramAdvance);
					if (!generic)
						landStatic();
					// A branch slot must leave nothing for the slots it may skip: no read in flight, no
					// command armed, and it must not itself be a second word.
					if (_jumps && slot.op == DspSlotOp::branch && slot.branch != DspBranch::never)
					{
						if (generic || pendingKind != DspEramArm::none || slot.eramArm != DspEramArm::none ||
							queue[0].countdown >= 0 || queue[1].countdown >= 0)
							return false;
					}

					bool portBusy = false;
					if (generic)
						emit(FlatOpKind::eramGeneric, static_cast<uint8_t>(slot.eramArm), 0, 0, pc);
					// The pairing is known from the resync slot on, even where the ops are still generic.
					if (pc > resync)
					{
						if (pendingKind != DspEramArm::none)
						{
							if (!generic)
							{
								if (pendingKind == DspEramArm::read)
								{
									const auto e = allocateEntry();
									queue[e] = {1, false, pc};
								}
								else
									emit(FlatOpKind::eramWrite, 0, 0, 0, pc);
							}
							pendingKind = DspEramArm::none;
							portBusy = true;
						}
						else if (slot.eramArm != DspEramArm::none)
						{
							if (!generic)
							{
								emit(FlatOpKind::eramArm, slot.eramArm == DspEramArm::writeIramLatch ? 1 : 0);
								flat.carry.hasArm = 1;
								flat.carry.lastArmSlot = pc;
								flat.carry.lastArmIsWrite = slot.eramArm != DspEramArm::read;
							}
							pendingKind = slot.eramArm;
						}
					}

					emitTransferWrites(slot);
					emitIo(slot, false);

					if (slot.op == DspSlotOp::indexedEramRead)
					{
						if (generic)
							emit(FlatOpKind::eramIndexedGeneric);
						else if (!portBusy)
						{
							const auto e = allocateEntry();
							queue[e] = {2, true, pc};
							emit(FlatOpKind::eramIndexed, static_cast<uint8_t>(e));
						}
					}

					emitDatapath(slot, pc);
					emitTransferReads(slot);
					emitConsume(slot, false);
				}
				for (size_t pc = emitCount; pc <= dsp::nProgramSlots; ++pc)
					flat.slotToOp[pc] = static_cast<uint16_t>(flat.ops.size());

				auto& carry = flat.carry;
				carry.outputWordPosition = wordPosition;
				carry.dacPortPosition = static_cast<uint8_t>(ordinal % 3);
				carry.serialOutputCount[0] = static_cast<uint8_t>(countA);
				for (size_t port = 0; port < 3; ++port)
					carry.serialOutputCount[1 + port] = static_cast<uint8_t>(countBcd[port]);
				carry.staticRegion = staticStart < slotCount;
				carry.prefixPendingAtEnd = pendingKind != DspEramArm::none;
				for (int e = 0; e < 2; ++e)
					carry.queue[e] = {static_cast<int8_t>(queue[e].countdown), queue[e].indexed, queue[e].slot};
				return true;
			}
		};
	} // namespace

	void flatLower(const DspProgram& _program, const DspFrameConfig& _config, FlatProgram& _flat, DspParams& _params)
	{
		dspRefreshParams(_program, _params);
		_flat.ops.clear();
		_flat.slotToOp.fill(0);
		_flat.config = _config;
		_flat.dynamic = false;
		_flat.jumps = false;
		_flat.slotsLowered = 0;
		_flat.staticStart = 0;
		_flat.mixerCells = {};
		_flat.carry = FlatCarry{};

		Lowering lowering{_config, _flat};
		for (size_t pc = 0; pc < dsp::nProgramSlots; ++pc)
			dspDecodeSlot(_program.pram[pc], _program.cram[pc], pc, _config.iram3ParameterReadBoundary,
						  lowering.slots[pc]);

		bool branches = false;
		for (size_t pc = 0; pc < _config.executionSlots; ++pc)
			if (lowering.slots[pc].op == DspSlotOp::branch && lowering.slots[pc].branch != DspBranch::never)
				branches = true;
		if (!branches)
		{
			lowering.lowerStatic(false, 0);
			return;
		}
		uint16_t lastSlot = 0;
		if (!_config.linked && lowering.jumpsEligible(lastSlot))
		{
			Lowering attempt{_config, _flat};
			attempt.slots = lowering.slots;
			if (attempt.lowerStatic(true, lastSlot))
				return;
			_flat.ops.clear();
			_flat.slotToOp.fill(0);
			_flat.mixerCells = {};
			_flat.carry = FlatCarry{};
		}
		lowering.lowerDynamic();
	}

} // namespace xpLib
