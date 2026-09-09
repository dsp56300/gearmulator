/*
 * High-level emulation of the SC-55 MIDI sub-MCU.
 * See sc55_submcu.h for the board interface.
 */
#include "sc55_submcu.h"

#include <cstring>

namespace emu88Lib
{

// Shared-RAM offsets, as the main MCU sees them at $EC00 + n.
enum
{
    SH_HOST_OUT = 0x00,     // .. 0x0f  main MCU -> sub, access-flag gated
    SH_TO_HOST0 = 0x20,
    SH_TO_HOST1 = 0x21,
    SH_VERSION  = 0x28,     // .. 0x2a  "101"
    SH_RING_C   = 0x40,     // .. 0x9f  COMPUTER port receive ring
    SH_ROUTE_A  = 0xb0,
    SH_ROUTE_B  = 0xb1,
    SH_ROUTE_C  = 0xb2,
    SH_ROUTE_D  = 0xb3,
    SH_ROUTE_E  = 0xb4,
    SH_ROUTE_F  = 0xb5,
    SH_OUT_SEL  = 0xb6,
    SH_PORT_MODE= 0xb7,
    SH_HOST_CMD = 0xb9,
    SH_OPT_FLAGS= 0xba
};

// Timing and flow-control parameters.
enum
{
    AS_TX_TICKS   = 25,     // 312.5 ms between $FE
    AS_WATCHDOG   = 50,     // 625 ms of silence
    BLOCK_TICKS   = 150,    // 1.875 s output mute
    RTS_PRELOAD   = 0x5c,
    LOCK_MIDI     = 8,      // outOwner tag shared by sources A and B
    LOCK_HOST     = 9       // outOwner tag for source D
};

Sc55SubMcu::Sc55SubMcu()
    : m_hostWrote(false)
{
    reset();
}

void Sc55SubMcu::reset()
{
    memset(&m_s, 0, sizeof(m_s));

    // The first shared-RAM block receives host writes; the remaining blocks supply host reads.
    m_s.ramDir = 0x01;

    // Version identifier expected by the main MCU.
    m_s.shared[SH_VERSION + 0] = '1';
    m_s.shared[SH_VERSION + 1] = '0';
    m_s.shared[SH_VERSION + 2] = '1';

    m_s.semaphore = 0x87;           // bit 7 = idle
    m_s.hostIndex = 0xff;
    m_s.asTxCount = AS_TX_TICKS;
    m_s.rtsCount  = RTS_PRELOAD;
    m_s.nextTick  = kTickCycles;
    m_s.running   = 0;              // wait for the boot handshake
}

void Sc55SubMcu::initFromConfig()
{
    // PC mode overrides the COMPUTER-port routing.
    if (m_s.shared[SH_PORT_MODE] == 6)
    {
        m_s.shared[SH_ROUTE_C] = 7;
        m_s.shared[SH_OUT_SEL] = 6;
    }

    // Only enabled physical outputs receive Active Sensing.
    const uint8_t outSel = m_s.shared[SH_OUT_SEL];
    m_s.out[0].enabled = (m_s.shared[SH_ROUTE_D] == 4 ||
                          m_s.shared[SH_ROUTE_E] == 4 ||
                          m_s.shared[SH_ROUTE_F] == 4 ||
                          outSel == 4) ? 1 : 0;
    m_s.out[1].enabled = (outSel == 6) ? 1 : 0;

    // Start with COMPUTER-port flow control deasserted.
    m_s.rtsCount    = RTS_PRELOAD;
    m_s.rtsAsserted = 0;
    if (m_hooks.computerRts)
        m_hooks.computerRts(0);
}

void Sc55SubMcu::update(uint64_t mcuCycles)
{
    if (mcuCycles <= m_s.cycles)
        return;
    m_s.cycles = mcuCycles;

    while (m_s.cycles >= m_s.nextTick)
    {
        m_s.nextTick += kTickCycles;
        if (m_s.running)
            tick();
    }

    if (!m_s.running)
        return;

    serviceInputs();
    drainTx();
    pumpAll();
    reportErrors();
}

void Sc55SubMcu::tick()
{
    // Periodic Active Sensing also resets transmit running status.
    if (--m_s.asTxCount == 0)
    {
        m_s.asTxCount = AS_TX_TICKS;
        m_s.out[0].lastStatus = 0;
        m_s.out[1].lastStatus = 0;
        for (int p = 0; p < 2; p++)
        {
            SerialOut& o = m_s.out[p];
            const bool idle = (o.wr == o.rd) && o.txFreeAt <= m_s.cycles;
            if (o.enabled && !o.blocked && idle)
            {
                o.txFreeAt = m_s.cycles + (p == 0 ? kMidiByteCycles
                                                  : computerByteCycles());
                if (p == 0 && m_hooks.midiOut)
                    m_hooks.midiOut(0xfe);
                else if (p == 1 && m_hooks.computerOut)
                    m_hooks.computerOut(0xfe);
            }
        }
    }

    // Each input's watchdog is armed by its first Active Sensing byte.
    for (int s = SRC_A; s <= SRC_C; s++)
    {
        MidiSource& m = m_s.src[s];
        if (m.asWatchdog && --m.asWatchdog == 0)
            m.overflowPending |= 2;         // bit 1 = Active Sensing lost
    }

    for (int p = 0; p < 2; p++)
    {
        SerialOut& o = m_s.out[p];
        if (o.blockTimer && --o.blockTimer == 0)
            o.blocked = 0;
    }

    // Timing approximation: repeat unacknowledged IRQs because this model can
    // deliver data before the main MCU unmasks its interrupt input. Repeat only
    // the pulse: restoring status bits could make the host consume a byte twice.
    if (m_s.ipce[0])
        pulseIrq();
}

uint64_t Sc55SubMcu::computerByteCycles() const
{
    // PC mode uses 38400 baud; other modes use the MIDI rate.
    return m_s.shared[SH_PORT_MODE] == 6 ? kFastByteCycles : kMidiByteCycles;
}

void Sc55SubMcu::pushInput(int src, uint8_t data)
{
    uint16_t next = (uint16_t)((m_s.inWr[src] + 1) % kInQueue);
    if (next == m_s.inRd[src])
        return;                             // wire-level overrun, byte lost
    m_s.inQueue[src][m_s.inWr[src]] = data;
    m_s.inWr[src] = next;
}

void Sc55SubMcu::serviceInputs()
{
    for (int s = SRC_A; s <= SRC_C; s++)
    {
        const uint64_t byteCycles =
            (s == SRC_C) ? computerByteCycles() : kMidiByteCycles;
        while (m_s.inRd[s] != m_s.inWr[s] && m_s.rxFreeAt[s] <= m_s.cycles)
        {
            uint8_t b = m_s.inQueue[s][m_s.inRd[s]];
            m_s.inRd[s] = (uint16_t)((m_s.inRd[s] + 1) % kInQueue);
            m_s.rxFreeAt[s] = m_s.cycles + byteCycles;
            receiveByte(s, b);
        }
    }
}

void Sc55SubMcu::receiveByte(int src, uint8_t data)
{
    MidiSource& m = m_s.src[src];

    // Route 0 disables COMPUTER input.
    if (src == SRC_C && m_s.shared[SH_ROUTE_C] == 0)
        return;

    // Traffic refreshes an armed watchdog; Active Sensing arms it and is consumed locally.
    if (m.asWatchdog)
        m.asWatchdog = AS_WATCHDOG;
    if (data == 0xfe)
    {
        m.asWatchdog = AS_WATCHDOG;
        return;
    }

    if (src == SRC_C)
    {

        uint8_t next = (uint8_t)(m_s.rxCwr + 1);
        if (next >= kRingC)
            next = 0;
        m_s.ringC[m_s.rxCwr] = data;
        m_s.shared[SH_RING_C + m_s.rxCwr] = data;
        if (next == m_s.rxCrd)
        {
            m.overflowPending |= 1;         // write pointer not advanced
            return;
        }
        // Assert flow control when the receive buffer is nearly full.
        if (m_s.shared[SH_OPT_FLAGS] & 1)
        {
            if ((int8_t)(--m_s.rtsCount) < 0)
                setRts(true);
        }
        m_s.rxCwr = next;
        m_s.wakeC = 1;
        return;
    }

    uint8_t next = (uint8_t)((m.wr + 1) & 0x0f);
    m.ring[m.wr] = data;
    if (next == m.rd)
    {
        m.overflowPending |= 1;             // write pointer not advanced
        return;
    }
    m.wr = next;
}

// Expand running status and return the next complete-message byte, or false if none is ready.
bool Sc55SubMcu::fetch(int src, uint8_t& out)
{
    MidiSource& m = m_s.src[src];

    if (m.idx)
    {
        out = m.msg[m.idx];
        if (--m.cnt == 0)
        {
            m.idx  = 0;
            m.busy = 0;
        }
        else
        {
            m.idx++;
        }
        return true;
    }

    for (;;)
    {

        uint8_t b;
        if (src == SRC_C)
        {
            if (m_s.rxCrd == m_s.rxCwr)
                return false;
            b = m_s.ringC[m_s.rxCrd];
            uint8_t next = (uint8_t)(m_s.rxCrd + 1);
            if (next >= kRingC)
                next = 0;
            m_s.rxCrd = next;
            // Release flow control as receive-buffer space becomes available.
            if (m_s.shared[SH_OPT_FLAGS] & 1)
            {
                if ((int8_t)(++m_s.rtsCount) >= 0)
                    setRts(false);
            }
        }
        else
        {
            if (m.rd == m.wr)
                return false;
            b = m.ring[m.rd];
            m.rd = (uint8_t)((m.rd + 1) & 0x0f);
        }

        if (b >= 0x80)
        {
            if (b >= 0xf8)                          // System Real Time
            {
                out = b;                            // passes through mid-message
                return true;
            }
            m.expect2 = 0;
            m.rs      = b;
            if (b < 0xf0)                           // channel status:
                continue;                           // absorb, wait for data
            if (b == 0xf0)                          // SysEx start
            {
                m.busy = 1;
                out    = b;
                return true;
            }
            if (b == 0xf6 || b == 0xf7)
            {
                m.rs   = 0;
                m.busy = 0;
                out    = b;
                return true;
            }
            if (b == 0xf4 || b == 0xf5)
                m.rs = 0;
            continue;
        }

        if (m.expect2)
        {
            m.msg[2]  = b;
            m.expect2 = 0;
            m.busy    = 1;
            m.idx     = 1;
            m.cnt     = 2;
            out       = m.msg[0];
            return true;
        }

        const uint8_t st = m.rs;
        if (st == 0)
            continue;                               // orphan data byte, dropped
        if (st == 0xf0)                             // inside SysEx
        {
            out = b;
            return true;
        }

        m.msg[0] = st;
        m.msg[1] = b;
        if (st >= 0xc0 && st < 0xe0)                // one data byte
        {
            m.idx  = 1;
            m.cnt  = 1;
            m.busy = 1;
            out    = m.msg[0];
            return true;
        }
        if (st < 0xf0)                              // two data bytes
        {
            m.expect2 = 1;
            continue;
        }
        m.rs = 0;                                   // system common
        if (st == 0xf2)                             // Song Position: two bytes
        {
            m.expect2 = 1;
            continue;
        }
        m.idx  = 1;
        m.cnt  = 1;
        m.busy = 1;
        out    = m.msg[0];
        return true;
    }
}

// The main MCU's outgoing bytes sit in $EC00-$EC0F.  A slot is valid only
// while its hardware access flag is set, so reading one consumes it.
bool Sc55SubMcu::fetchFromHostBuf(uint8_t& out)
{
    if (m_s.hostIndex & 0x80)
        return false;

    const uint8_t y = m_s.hostIndex;
    if (y >= kHostOutSize || !flagGet((uint8_t)(SH_HOST_OUT + y)))
    {
        // Notify the main MCU that it can refill the outgoing buffer.
        m_s.hostIndex = 0xff;
        raiseStatus(ST_HOSTBUF_END);
        pulseIrq();
        return false;
    }

    const uint8_t b = m_s.shared[SH_HOST_OUT + y];
    flagClear((uint8_t)(SH_HOST_OUT + y));

    // Release arbitration ownership at message boundaries.
    if (b & 0x80)
    {
        if (b == 0xf7)
        {
            m_s.dInSysex = 0;
        }
        else
        {
            m_s.dInSysex = 1;
            m_s.dStatus  = b;
            m_s.dCount   = (uint8_t)(b < 0xc0 ? 2 : 1);
        }
    }
    else if (m_s.dStatus < 0xf0)
    {
        if (--m_s.dCount == 0)
            m_s.dInSysex = 0;
    }

    m_s.hostIndex = (uint8_t)(y + 1);
    out = b;
    return true;
}

Sc55SubMcu::Put Sc55SubMcu::putSerial(int port, uint8_t data, bool filtered)
{
    SerialOut& o = m_s.out[port];

    if (!filtered)
    {
        if (o.blocked)
        {
            o.suppress = 1;
            return PUT_SWALLOWED;
        }

        if (data >= 0x80)
        {
            if (data < 0xf8)
            {
                o.suppress = 0;
                if (data < 0xf0)                    // running-status pack
                {
                    if (data == o.lastStatus)
                        return PUT_SWALLOWED;
                    o.lastStatus = data;
                }
                else if (data == 0xf0)
                {
                    o.lastStatus = data;
                }
                else if (data == 0xf7)
                {
                    o.lastStatus = 0;
                }
            }
        }
        else if (o.suppress)
        {
            return PUT_SWALLOWED;
        }
    }

    // Transmit immediately when idle; otherwise queue the byte.
    if (o.wr == o.rd && o.txFreeAt <= m_s.cycles)
    {
        emitSerial(port, data);
        return PUT_SENT;
    }

    // A full FIFO defers delivery until a later update frees space.
    uint8_t next = (uint8_t)((o.wr + 1) & (kTxFifo - 1));
    if (next == o.rd)
        return PUT_DEFERRED;
    o.fifo[o.wr] = data;
    o.wr = next;
    return PUT_SENT;
}

void Sc55SubMcu::emitSerial(int port, uint8_t data)
{
    SerialOut& o = m_s.out[port];
    o.txFreeAt = m_s.cycles +
        (port == 0 ? kMidiByteCycles : computerByteCycles());
    if (port == 0)
    {
        if (m_hooks.midiOut)
            m_hooks.midiOut(data);
    }
    else if (m_hooks.computerOut)
    {
        m_hooks.computerOut(data);
    }
}

void Sc55SubMcu::drainTx()
{
    for (int p = 0; p < 2; p++)
    {
        SerialOut& o = m_s.out[p];
        while (o.wr != o.rd && o.txFreeAt <= m_s.cycles)
        {
            uint8_t b = o.fifo[o.rd];
            o.rd = (uint8_t)((o.rd + 1) & (kTxFifo - 1));
            emitSerial(p, b);
        }
    }
}

Sc55SubMcu::Put Sc55SubMcu::putHost(int port, uint8_t data, bool filtered)
{
    if (!filtered)
    {
        if (data >= 0xf8)
            return PUT_SWALLOWED;

        if (data >= 0x80)
        {
            if (data < 0xf0)                        // running-status pack
            {
                if (data == m_s.hostRs[port])
                    return PUT_SWALLOWED;
                m_s.hostRs[port] = data;
            }
            else
            {
                m_s.hostRs[port] = 0;
            }
        }
    }

    // Each host port holds one byte until the main MCU reads it and clears the access flag.
    const uint8_t off = (uint8_t)(port == 0 ? SH_TO_HOST0 : SH_TO_HOST1);
    if (flagGet(off))
        return PUT_DEFERRED;

    m_s.shared[off] = data;
    flagSet(off);
    raiseStatus((uint8_t)(port == 0 ? ST_HOST0_READY : ST_HOST1_READY));
    pulseIrq();
    m_hostWrote = true;
    return PUT_SENT;
}

// Deliver to serial outputs before host ports, retaining progress across retries.
void Sc55SubMcu::deliver(uint8_t data, uint8_t& remaining, uint8_t& filtered)
{
    static const uint8_t order[4] = { DST_OUT2, DST_OUT1, DST_HOST0, DST_HOST1 };
    for (int i = 0; i < 4; i++)
    {
        const uint8_t bit = order[i];
        if (!(remaining & bit))
            continue;
        const bool done = (filtered & bit) != 0;
        Put r;
        if (bit == DST_OUT2)       r = putSerial(0, data, done);
        else if (bit == DST_OUT1)  r = putSerial(1, data, done);
        else if (bit == DST_HOST0) r = putHost(0, data, done);
        else                       r = putHost(1, data, done);
        if (r == PUT_DEFERRED)
        {
            filtered |= bit;
            continue;
        }
        remaining &= (uint8_t)~bit;
        filtered  &= (uint8_t)~bit;
    }
}

uint8_t Sc55SubMcu::destinationsFor(int src) const
{
    const uint8_t outSel = m_s.shared[SH_OUT_SEL];
    const uint8_t thru   = (uint8_t)(outSel == 4 ? DST_OUT2 : DST_OUT1);

    switch (src)
    {
    case SRC_A:
    case SRC_B:
    {
        const uint8_t route =
            m_s.shared[src == SRC_A ? SH_ROUTE_A : SH_ROUTE_B];
        if (route == 8) return thru;
        if (route == 1) return DST_HOST0;
        return DST_HOST1;
    }
    case SRC_C:
        return (uint8_t)(DST_OUT2 | DST_HOST0);
    default:
        return m_s.shared[SH_ROUTE_D] == 4 ? (uint8_t)DST_OUT2 : thru;
    }
}

// Arbitration separates serial-input messages from host messages. A and B share
// ownership and can interleave with each other; COMPUTER input bypasses the lock.
bool Sc55SubMcu::lockAvailable(int src) const
{
    if (src == SRC_C)
        return true;
    if (src == SRC_D)
    {
        if (m_s.shared[SH_ROUTE_D] == 4)            // takes no lock
            return true;
        return m_s.outOwner == 0 || m_s.outOwner == LOCK_HOST;
    }
    if (m_s.shared[src == SRC_A ? SH_ROUTE_A : SH_ROUTE_B] != 8)
        return true;                                // host-port routes: no lock
    return m_s.outOwner == 0 || m_s.outOwner == LOCK_MIDI;
}

void Sc55SubMcu::updateLock(int src)
{
    if (src == SRC_C)
        return;
    if (src == SRC_D)
    {
        if (m_s.shared[SH_ROUTE_D] == 4)
            return;
        m_s.outOwner = m_s.dInSysex ? LOCK_HOST : 0;
        return;
    }
    if (m_s.shared[src == SRC_A ? SH_ROUTE_A : SH_ROUTE_B] != 8)
        return;
    m_s.outOwner = m_s.src[src].busy ? LOCK_MIDI : 0;
}

void Sc55SubMcu::pumpSource(int src)
{
    if (!lockAvailable(src))
        return;
    if (src == SRC_C && !m_s.wakeC)
        return;

    for (int guard = 0; guard < 256; guard++)
    {
        MidiSource& m = m_s.src[src];

        if (m.pending)
        {
            m_hostWrote = false;
            deliver(m.pendingByte, m.pendingDst, m.pendingFiltered);
            if (m.pendingDst)
                return;
            m.pending = 0;
            updateLock(src);
            // COMPUTER input yields after each host delivery.
            if (src == SRC_C && m_hostWrote)
            {
                m_s.wakeC = 0;
                return;
            }
        }

        uint8_t b;
        if (src == SRC_D)
        {
            if (!fetchFromHostBuf(b))
                return;
        }
        else if (!fetch(src, b))
        {
            return;
        }

        m.pending         = 1;
        m.pendingByte     = b;
        m.pendingDst      = destinationsFor(src);
        m.pendingFiltered = 0;
    }
}

void Sc55SubMcu::pumpAll()
{
    // A host request restarts the outgoing-buffer scan.
    if (m_s.kickHostBuf)
    {
        m_s.kickHostBuf = 0;
        m_s.hostIndex   = 0;
    }

    pumpSource(SRC_A);
    pumpSource(SRC_B);
    pumpSource(SRC_C);
    pumpSource(SRC_D);
}

void Sc55SubMcu::reportErrors()
{
    for (int s = SRC_A; s <= SRC_C; s++)
    {
        MidiSource& m = m_s.src[s];
        if (!m.overflowPending)
            continue;

        const uint8_t route =
            m_s.shared[s == SRC_A ? SH_ROUTE_A
                     : s == SRC_B ? SH_ROUTE_B : SH_ROUTE_C];

        if (m.overflowPending & 1)                  // ring overrun
        {
            if (s == SRC_C)
            {
                raiseStatus(ST_OVERFLOW0);
            }
            else
            {
                m.busy = 0;
                if (route == 8)
                {
                    if (m_s.outOwner == LOCK_MIDI)
                        m_s.outOwner = 0;
                    raiseStatus(ST_OVERFLOW1);
                }
                else
                {
                    raiseStatus(route == 1 ? ST_OVERFLOW0 : ST_OVERFLOW1);
                }
            }
            pulseIrq();
        }

        if (m.overflowPending & 2)                  // Active Sensing lost
        {
            raiseStatus((uint8_t)(s == SRC_A ? ST_AS_LOST_A
                                : s == SRC_B ? ST_AS_LOST_B : ST_AS_LOST_C));
            pulseIrq();

            // Temporarily mute this source's output while the main MCU sends note-offs.
            const bool feedsOutput = (s == SRC_C) ? (route == 7) : (route == 8);
            if (feedsOutput)
            {
                if (s != SRC_C)
                {
                    m.busy          = 0;
                    m_s.outOwner    = 0;
                    m_s.kickHostBuf = 1;
                }
                const int p = (s == SRC_C) ? 0
                            : (m_s.shared[SH_OUT_SEL] == 4 ? 0 : 1);
                m_s.out[p].blocked    = 1;
                m_s.out[p].blockTimer = BLOCK_TICKS;
            }
        }

        m.overflowPending = 0;
    }
}

void Sc55SubMcu::setRts(bool asserted)
{
    const uint8_t level = asserted ? 1 : 0;
    if (m_s.rtsAsserted == level)
        return;
    m_s.rtsAsserted = level;
    if (m_hooks.computerRts)
        m_hooks.computerRts(level);
}

void Sc55SubMcu::raiseStatus(uint8_t bits)
{
    m_s.ipce[0] |= bits;
}

void Sc55SubMcu::pulseIrq()
{
    if (m_hooks.hostIrq)
    {
        m_hooks.hostIrq(1);
        m_hooks.hostIrq(0);
    }
}

void Sc55SubMcu::handleCommand(uint8_t cmd)
{
    if (cmd & 0x80)
    {
        reset();
        return;
    }
    if (cmd & 0x01)
    {
        if (!m_s.running)
        {
            m_s.running = 1;
            initFromConfig();
        }
    }
    if (cmd & 0x02)
        m_s.kickHostBuf = 1;
    if ((cmd & 0x10) && m_s.shared[SH_ROUTE_C] == 7)
        m_s.wakeC = 1;
    // Command bit 5 has no modeled effect.

    m_s.shared[SH_HOST_CMD] = 0;
    m_s.semaphore = 0x87;
}

uint8_t Sc55SubMcu::hostRead(uint32_t address)
{
    const uint8_t off = (uint8_t)(address & 0xff);

    if (off < kSharedSize)
    {
        // A host read clears the access flag for blocks flowing sub -> host.
        if ((m_s.ramDir & (1 << (off >> 5))) == 0)
            flagClear(off);
        return m_s.shared[off];
    }
    if (off == 0xf5)
        return m_hooks.readP1 ? m_hooks.readP1() : 0xff;
    if (off == 0xf6)
        return m_hooks.readP0 ? m_hooks.readP0() : 0xff;
    if (off == 0xf7)
        return m_s.p0dir;
    if (off >= 0xf8 && off < 0xfc)
    {
        const uint8_t v = m_s.ipce[off & 3];
        m_s.ipce[off & 3] = 0;                      // read-to-clear
        return v;
    }
    if (off == 0xff)
        return m_s.semaphore;
    return 0xff;
}

void Sc55SubMcu::hostWrite(uint32_t address, uint8_t data)
{
    const uint8_t off = (uint8_t)(address & 0xff);

    if (off < kSharedSize)
    {
        flagSet(off);
        m_s.shared[off] = data;
        // Keep the mirror of the COMPUTER receive ring coherent if the main
        // MCU ever writes into that window.
        if (off >= SH_RING_C && off < SH_RING_C + kRingC)
            m_s.ringC[off - SH_RING_C] = data;
        return;
    }
    if (off == 0xf5)
    {
        if (m_hooks.writeP1)
            m_hooks.writeP1(data);
        return;
    }
    if (off == 0xf6)
    {
        if (m_hooks.writeP0)
            m_hooks.writeP0(data);
        return;
    }
    if (off == 0xf7)
    {
        m_s.p0dir = data;
        return;
    }
    if (off >= 0xf8 && off < 0xfc)
    {
        m_s.ipcm[off & 3] = data;
        if ((off & 3) == 0)
        {
            // Writing $ECF8 signals a command; its payload is in SH_HOST_CMD.
            m_s.semaphore &= (uint8_t)~0x80;
            handleCommand(m_s.shared[SH_HOST_CMD]);
        }
        return;
    }
    if (off == 0xff)
        m_s.semaphore = (uint8_t)((m_s.semaphore & ~0x1f) | (data & 0x1f));
}

}	// namespace emu88Lib
