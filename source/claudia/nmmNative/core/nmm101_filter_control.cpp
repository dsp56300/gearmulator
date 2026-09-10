#include "nmm101_filter_control.h"

#ifdef NMM_FILTER_CONTROL_TRACE
#include <cstdio>
#endif

namespace nmm::native
{
namespace
{
// EXTRACTU uses x0 as a packed width/offset control in normal 56300 mode.
// The native accumulator is right-aligned, while the emulator stores A/B
// left-aligned; shifting the emulator value by eight therefore leaves the
// same 56-bit field represented by this helper.
Word24 extractUnsigned(Word24 control,Acc56 source) noexcept
{
    const unsigned width=(control>>12)&0x3fu;
    const unsigned offset=control&0x3fu;
    if(width==0 || offset>=56) return 0;
    const auto bits=static_cast<uint64_t>(source);
    const uint64_t mask=width>=64?~uint64_t(0):((uint64_t(1)<<width)-1);
    return static_cast<Word24>((bits>>offset)&mask);
}

bool leftShiftLessThan(Acc56 before,Acc56 after,unsigned count) noexcept
{
    // ASL sets V when the mathematical shift cannot be represented in the
    // signed 56-bit accumulator.  LT is N xor V, while MI is N.
    const auto minBefore=-(static_cast<Acc56>(1)<<(55-count));
    const auto maxBefore=(static_cast<Acc56>(1)<<(55-count))-1;
    const bool overflow=before<minBefore || before>maxBefore;
    return (after<0)!=overflow;
}

bool subtractionLessThan(Acc56 difference) noexcept
{
    // The emulator's CMP clears V and leaves N from its wrapped B-X0 result;
    // TLT therefore observes the sign bit of that 56-bit difference even
    // when the mathematical subtraction overflows.
    return difference<0;
}
}

Word24 runFilterF92Control(ModuleCursor& c) noexcept
{
    auto& m=*c.memory;
#ifdef NMM_FILTER_CONTROL_TRACE
    auto trace=[&](const char* p){std::fprintf(stderr,"native %s a=%lld b=%lld x0=%06x x1=%06x y0=%06x y1=%06x r2=%zx r3=%zx r4=%zx r5=%zx\n",p,(long long)c.a,(long long)c.b,c.x0,c.x1,c.y0,c.y1,c.r2,c.r3,c.r4,c.r5);};
#else
    auto trace=[](const char*){};
#endif

    // P:$0175: move x:(r3)+,x1 y:(r4)+,y0
    c.x1=c.xReadInc(); c.y0=c.yReadInc();
    trace("175");
    // P:$0176: mpy +x1,x0,a x:$000d,x1
    c.a=mpy(c.x1,c.x0); c.x1=m.readX(0x0d);
    trace("176");
    // P:$0177: mac +x1,y0,a x:(r3)+,x0 y:(r4)+,y0
    c.a=mac(c.a,c.x1,c.y0); c.x0=c.xReadInc(); c.y0=c.yReadInc();
    trace("177");
    // P:$0178..$017e: clear x1, finish the pointer calculation and extract.
    c.x1=0;
    c.a=mac(c.a,c.x1,c.y0);
    c.a=asl(c.a,2);
    // P:$017c `move a,a` is a 24-bit A1 transfer back into A, rather than a
    // no-op full-register move.  Keep that narrowing explicit before the
    // following EXTRACTU/ASR pair.
    c.a=from24(limit24(c.a));
    c.b=extractUnsigned(c.x0,c.a);
    c.a=asr(c.a,16);
    trace("17e");
    // P:$017f..$0180
    c.r5=limit24(c.b); c.r2=limit24(c.a);

    // P:$0183..$0185: coefficient lookup and first product.
    c.x0=c.xReadInc();
    c.y1=m.readY(c.r5); c.r5=mask24(c.r5+c.n5);
    c.y1=m.readY(c.r5);
    c.b=mpy(c.x0,c.y1); c.y1=c.yReadInc();
    // P:$0186 is the long displacement x:(r2+1920).
    c.x0=m.readX(mask24(c.r2+1920));
    c.y0=limit24(c.b);
    c.b=mpy(c.y0,c.x0); c.r5=mask24(c.yReadInc());
    const auto shiftedInput=mpy(c.y0,c.x0);
    c.b=asl(shiftedInput,12);
    const bool shiftedLess=leftShiftLessThan(shiftedInput,c.b,12);
    const bool shiftedMinus=c.b<0;
    if(shiftedLess) c.b=from24(c.y1);
    if(shiftedMinus) c.b=from24(c.y1);
    c.b=neg(c.b);
    const auto oldB=c.b;
    c.b=add(c.b,from24(c.y1)); c.x0=limit24(oldB);
    trace("18f");

    // P:$0190..$0198: reciprocal shaping and coefficient clamp.
    c.a=mpyr(c.x0,c.x0);
    c.x1=limit24(c.a); c.y0=c.yReadInc();
    c.a=mpyr(c.x1,c.x0);
    c.b=mac(c.b,c.x1,c.y0); c.y0=c.yReadInc();
    c.x1=limit24(c.a);
    c.b=mac(c.b,c.x1,c.y0); c.x0=c.xReadInc();
    const auto comparison=sub(c.b,from24(c.x0));
    if(subtractionLessThan(comparison)) c.b=from24(c.x0);
    trace("198");

    // P:$0199..$019a is the final filter-control prefetch/store. The envelope
    // begins at P:$019b and loads X1/B itself when called with preloaded=true.
    c.x0=c.xReadInc(); c.y0=c.yReadInc();
    m.writeX(c.r5,limit24(c.b));
    trace("19a");
    return c.x0;
}
}
