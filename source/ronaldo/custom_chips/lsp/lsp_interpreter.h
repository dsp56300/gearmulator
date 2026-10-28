#pragma once

#include "lsp_program.h"

namespace lspLib
{
	// Reference engine. Keeps the accumulator pipeline as the silicon does —
	// three shift stages per accumulator — so it is exact across jumps and is
	// what the JIT is validated against. Everything odd-looking below is
	// hardware-validated behaviour: MUL applies its scaler after multiplying
	// and a further 8 for the low-half coefficient, whose operand is the
	// previous instruction's multiplier input, select bits 00 multiply by the
	// coefficient byte, MUL adds to the unsaturated accumulator, every register
	// special accumulates, ERAM keeps a float word, slot 0x13 splits one raw accumulator into
	// the second ERAM tap offset and coefficient 1, and stores read the
	// accumulator three instructions back.
	class LSPInterpreter
	{
		static int64_t operand(const LSPInstr& _i, const LSPRuntime& _rt)
		{
			if(_i.opFromLatch)
				return _rt.mulInput;
			return _i.opImmShift ? int64_t{1} << _i.opImmShift : _rt.readRing(_i.opMem);
		}

	public:
		static void runProgram(const LSPProgram& _p, LSPRuntime& _rt)
		{
			_rt.audioIn = _rt.audioInR;
			bool pred = _rt.jumpPending != 0;

			uint32_t total = 0;
			for(uint32_t pc = 0; pc < ProgramWords && total < ProgramWords; ++pc, ++total)
			{
				const LSPInstr& i = _p.instr[pc];

				if(i.eramRead)
				{
					const uint32_t addr = (_rt.eramPos + _p.eramAddr[pc]
						+ (i.eramSecondTap ? static_cast<uint32_t>(_rt.eramSecondTapOffs) : 0u)) & EramMask;
					_rt.eramReadValue = static_cast<int32_t>(static_cast<uint32_t>(_rt.eram[addr]) << 4);
				}

				int32_t* a = _rt.accs;
				int32_t newA = a[2];
				int32_t newB = a[5];

				if(i.op != Op::Skip)
				{
					int32_t src = 0;
					switch(i.src)
					{
					case Src::ASat: src = clamp24(a[0]); break;
					case Src::BSat: src = clamp24(a[3]); break;
					case Src::ARaw: src = signExtend24(a[0]); break;
					default: break;
					}
					int32_t& dest = i.accB ? newB : newA;
					const int64_t live = dest;
					const int32_t cc = _p.coefs[pc];

					switch(i.op)
					{
					case Op::Mac:
					{
						if(i.src != Src::None)
							_rt.writeRing(i.memOffs, src);
						const int64_t incr = i.immShift
							? (static_cast<int64_t>(cc) << i.immShift) >> i.scaler
							: (static_cast<int64_t>(_rt.readRing(i.opMem)) * cc) >> i.scaler;
						dest = static_cast<int32_t>(i.replace ? incr : live + incr);
						// abs judges the sign by bit 24 and negates the whole value
						if(i.abs && (dest & 0x1000000) != 0)
							dest = static_cast<int32_t>(0u - static_cast<uint32_t>(dest));
						break;
					}
					case Op::Mul:
					{
						const int32_t coef = i.mulCoef2 ? _rt.multiplCoef2 : _rt.multiplCoef1;
						const int32_t opB = i.mulCcCoef ? (i.mulLower ? static_cast<uint8_t>(i.cc) : i.cc)
							: i.mulLower ? (coef >> 8) & 0xff : coef >> 16;
						// A low half reads the previous multiplier input, before its own write-back.
						int64_t opA = i.mulLower ? operand(i, _rt) : 0;
						if(i.src != Src::None)
							_rt.writeRing(i.memOffs, src);
						if(!i.mulLower)
							opA = operand(i, _rt);
						int64_t result = i.mulZero ? 0 : opA * opB;
						result >>= i.scaler + (i.mulLower ? 8 : 0);
						if(i.mulNegate)
							result = -result;
						if(!i.replace)
							result += live;
						dest = static_cast<int32_t>(result);
						break;
					}
					case Op::Special:
						if(i.imm50d0)
						{
							const uint8_t ucc = static_cast<uint8_t>(cc);
							int64_t incr = (operand(i, _rt) * ucc) >> 7;
							incr >>= 1 + i.scaler;
							dest = static_cast<int32_t>(i.replace ? incr : live + incr);
							break;
						}
						switch(i.slot)
						{
						case SlotJumpIfNegative:    pred = src < 0; break;
						case SlotJumpIfNonNegative: pred = src >= 0; break;
						case SlotJumpAlways:        pred = true; break;
						case SlotEramWriteLatch:    _rt.eramWriteLatch = src; break;
						case SlotEramTapAndCoef1:
							if(i.src == Src::ARaw)
							{
								_rt.eramSecondTapOffs = a[0] >> 10;
								_rt.multiplCoef1 = (a[0] & 0x3ff) << 13;
							}
							break;
						case SlotMulCoef1: _rt.multiplCoef1 = src; break;
						case SlotMulCoef2: _rt.multiplCoef2 = src; break;
						case SlotRing76: _rt.writeRing(0x76, src); break;
						case SlotAudioOut:
							_rt.audioOut = src;
							_rt.writeRing(0x78, src);
							break;
						case SlotEramRead0: case SlotEramRead0 + 1: case SlotEramRead0 + 2: case SlotEramRead0 + 3:
							src = _rt.eramReadValue;
							_rt.writeRing(static_cast<uint8_t>(0x60 + i.slot), src);
							break;
						case SlotAudioIn: case SlotAudioIn2:
							if(pc >= ChannelSplit)
								_rt.audioIn = _rt.audioInL;
							src = _rt.audioIn;
							_rt.writeRing(static_cast<uint8_t>(0x60 + i.slot), src);
							break;
						default:
							break;
						}
						if(i.latchesSrc)
							_rt.mulInput = src;
						if(i.writesAcc)
						{
							const int64_t incr = (static_cast<int64_t>(src) * cc) >> i.scaler;
							dest = static_cast<int32_t>(i.replace ? incr : live + incr);
						}
						break;
					default:
						break;
					}
				}

				a[0] = a[1]; a[1] = a[2]; a[2] = newA;
				a[3] = a[4]; a[4] = a[5]; a[5] = newB;

				if(i.eramWrite)
				{
					// a write cycle leaves its data in the read latch too
					const int32_t word = eramQuantize(_rt.eramWriteLatch >> 4);
					_rt.eram[(_rt.eramPos + _p.eramAddr[pc]) & EramMask] = word;
					_rt.eramReadValue = static_cast<int32_t>(static_cast<uint32_t>(word) << 4);
				}

				if(i.jump == Jump::Skip)
				{
					// the skipped slots still pass as idle cycles: the history moves on
					a[0] = a[1] = a[2];
					a[3] = a[4] = a[5];
					pc = static_cast<uint32_t>(i.jumpDest - 1);
				}
				else if(i.jump != Jump::None && pred)
				{
					pc = static_cast<uint32_t>(i.jumpDest - 1);
					pred = false;
				}

				if(i.op == Op::Special && i.slot == SlotAudioOut && pc < ChannelSplit)
					_rt.audioOutR = _rt.audioOut;
			}

			_rt.jumpPending = pred;
			_rt.endOfPass();
		}
	};
}
