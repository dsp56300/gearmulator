#include "nmm101_envelope.h"

namespace nmm::native {
Word24 runEnvelope20Control(ModuleCursor& c, bool preloaded) noexcept {
    auto& m=*c.memory;
    if(!preloaded) {c.x0=c.xReadInc();c.y0=c.yReadInc();}
    c.x1=m.readX(c.r3);
    c.b=from24(c.yReadInc());
    // P:19c..1a0: zero input makes MPY/TFR/TST zero and TGT false.
    c.y1=0;c.a=0;c.xWriteInc(0);
    c.a=from24(m.readX(c.r3));
    const bool previousGateNonpositive=c.a<=0;
    c.a=from24(m.readX(0x0e));
    if(previousGateNonpositive) c.b=from24(c.x0);
    const bool gateNonpositive=c.a<=0;
    c.xWriteInc(limit24(c.a));
    if(gateNonpositive) c.b=from24(c.y0);

    c.r2=limit24(c.b);
    c.x1=limit24(c.b);
    c.y0=c.yReadDec();
    c.r3=m.readX(c.r3);
    c.x0=m.readX(c.r2++);
    c.a=from24(c.x0);
    c.y1=m.readX(c.r2++);
    c.a=sub(c.a,mpy(c.x0,c.y1));
    c.a=asl(c.a,1);
    c.x0=m.readX(c.r2++);
    c.a=mac(c.a,c.y1,c.y0);
    c.b=from24(static_cast<Word24>(c.r2));
    c.a=mac(c.a,c.y0,c.x0);
    c.x0=c.xReadInc();
    // TEC tests Extension Clear, not carry. In the 101 no-scaling mode,
    // E=0 iff accumulator bits 55..47 are all sign extension.
    if(c.a>=-0x800000000000ll && c.a<=0x7fffffffffffll)
        c.b=from24(c.x1);
    const auto previousA=c.a;
    c.a=from24(c.xReadInc());
    c.y0=limit24(previousA);
    c.a=mac(c.a,c.y0,c.x0);
    c.yWriteInc(limit24(c.b));
    c.x0=0x200000;
    c.x1=limit24(c.a);
    c.yWriteInc(c.y0);
    c.a=mpy(c.x1,c.x0);
    c.a=asl(c.a,2);
    m.writeX(0x13,limit24(c.a));
    return limit24(c.a);
}
}
