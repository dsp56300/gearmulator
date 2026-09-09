#pragma once

#include "lsp_program.h"

namespace lspLib
{
	// Reference engine. Keeps the accumulator pipeline as the silicon does —
	// three shift stages per accumulator — so it is exact across jumps and is
	// what the JIT is validated against. Everything odd-looking below is
	// hardware-validated behaviour: MUL applies its scaler after multiplying
	// and a further 7 for the low-half coefficient, a zero MUL coefficient
	// byte forces a zero product, slot 0x13 splits one raw accumulator into
	// the second ERAM tap offset and coefficient 1, and stores read the
	// accumulator three instructions back.
	class LSPInterpreter
	{
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
							: (static_cast<int64_t>(_rt.readRing(i.memOffs)) * cc) >> i.scaler;
						dest = static_cast<int32_t>(i.replace ? incr : live + incr);
						if(i.abs && dest < 0)
							dest = static_cast<int32_t>(0u - static_cast<uint32_t>(dest));
						break;
					}
					case Op::Mul:
					{
						int32_t opB = i.mulCoef2 ? _rt.multiplCoef2 : _rt.multiplCoef1;
						opB = i.mulLower ? (opB & 0xffff) >> 9 : opB >> 16;
						if(i.src != Src::None)
							_rt.writeRing(i.memOffs, src);
						const int64_t opA = i.immShift ? (1 << i.immShift) : _rt.readRing(i.memOffs);
						int64_t result = i.mulZero ? 0 : opA * opB;
						result >>= i.scaler + (i.mulLower ? 7 : 0);
						if(i.mulNegate)
							result = -result;
						if(!i.replace)
							result += clamp24(live);
						dest = static_cast<int32_t>(result);
						break;
					}
					case Op::Special:
						if(i.imm50d0)
						{
							const uint8_t ucc = static_cast<uint8_t>(cc);
							int64_t incr = (static_cast<int64_t>(_rt.readRing(i.prevMem)) * ucc) >> 7;
							if(i.prevMem >= 1 && i.prevMem <= 4)
								incr = static_cast<int64_t>(ucc) << ((i.prevMem - 1) * 5);
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
						case SlotAudioOut:
							_rt.audioOut = src;
							_rt.writeRing(0x78, src);
							break;
						case SlotEramRead0: case SlotEramRead0 + 1: case SlotEramRead0 + 2: case SlotEramRead0 + 3:
							src = _rt.eramReadValue;
							_rt.writeRing(static_cast<uint8_t>(0x60 + i.slot), src);
							break;
						case SlotAudioIn:
							if(pc >= ChannelSplit)
								_rt.audioIn = _rt.audioInL;
							src = _rt.audioIn;
							_rt.writeRing(0x7e, src);
							break;
						default:
							break;
						}
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
					_rt.eram[(_rt.eramPos + _p.eramAddr[pc]) & EramMask] = _rt.eramWriteLatch >> 4;

				if(i.jump != Jump::None && pred)
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
