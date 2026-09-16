/*
 * High-level emulation of the SC-55 MIDI sub-MCU.
 * Models serial MIDI routing, flow control and the host mailbox without
 * executing a sub-MCU ROM. The board supplies the panel I/O through Hooks.
 */
#pragma once

#include <cstdint>
#include <functional>

namespace emu88Lib
{

class Sc55SubMcu
{
public:

    // Main-MCU clock used by the timing model.
    static const uint64_t kMcuHz = 24000000;

    static const uint64_t kTickCycles = kMcuHz / 80;            // 12.5 ms

    // Serial byte times (10 bits per byte).
    static const uint64_t kMidiByteCycles = kMcuHz / 3125;      // 31250 baud
    static const uint64_t kFastByteCycles = kMcuHz / 3840;      // 38400 baud

    // Board wiring. Every hook is optional; an unset one behaves as if the
    // line were not connected.
    struct Hooks
    {
        std::function<void(uint8_t)> midiOut;      // a byte left MIDI OUT (UART2 TX)
        std::function<void(uint8_t)> computerOut;  // a byte left the COMPUTER port (UART1 TX)
        std::function<void(int)>     hostIrq;      // pulsed 1 then 0 to interrupt the main MCU
        std::function<void(int)>     computerRts;  // COMPUTER-port handshake line level
        std::function<uint8_t()>     readP1;       // button matrix rows
        std::function<void(uint8_t)> writeP1;
        std::function<uint8_t()>     readP0;
        std::function<void(uint8_t)> writeP0;      // button matrix column select
    };

    enum
    {
        kRingSmall   = 16,      // MIDI IN and MIDI IN 2 receive buffers
        kTxFifo      = 16,      // serial transmit buffers
        kRingC       = 96,      // source C, shared RAM $EC40-$EC9F
        kHostOutSize = 16,      // main MCU's outgoing slots $EC00-$EC0F
        kSharedSize  = 0xc0,
        kFlagBytes   = 0x18,
        kInQueue     = 512      // host-side arrival queue per serial input
    };

    // A = MIDI IN, B = MIDI IN 2, C = COMPUTER, D = main MCU.
    enum Source { SRC_A = 0, SRC_B = 1, SRC_C = 2, SRC_D = 3, SRC_COUNT = 4 };
    enum DstBit
    {
        DST_OUT2  = 1,          // MIDI OUT
        DST_OUT1  = 2,          // COMPUTER port
        DST_HOST0 = 4,          // $EC20
        DST_HOST1 = 8           // $EC21
    };

    // IPCE0 status bits, read by the main MCU at $ECF8.
    enum Status
    {
        ST_HOST0_READY = 0x01,
        ST_HOST1_READY = 0x02,
        ST_OVERFLOW0   = 0x04,
        ST_OVERFLOW1   = 0x08,
        ST_HOSTBUF_END = 0x10,
        ST_AS_LOST_A   = 0x20,
        ST_AS_LOST_B   = 0x40,
        ST_AS_LOST_C   = 0x80
    };

    struct MidiSource
    {
        uint8_t  ring[kRingSmall];  // A and B only; C lives in shared RAM
        uint8_t  wr, rd;
        uint8_t  msg[3];            // reassembled status, data1, data2
        uint8_t  idx, cnt;          // replay cursor into msg[]
        uint8_t  rs;                // running status
        uint8_t  expect2;           // waiting for a second data byte
        uint8_t  busy;              // mid-message, holds the arbitration lock
        uint8_t  asWatchdog;        // Active Sensing watchdog, 0 = disarmed
        uint8_t  overflowPending;
        uint8_t  pending;           // a fetched byte awaiting delivery
        uint8_t  pendingByte;
        uint8_t  pendingDst;        // destinations still to receive it
        // Retrying a blocked destination must not filter the same status byte twice.
        uint8_t  pendingFiltered;
    };

    struct SerialOut
    {
        uint8_t  fifo[kTxFifo];
        uint8_t  wr, rd;
        uint8_t  lastStatus;        // running-status packing
        uint8_t  blocked;           // muted after an Active Sensing loss
        uint8_t  suppress;          // drop data bytes of a swallowed message
        uint8_t  enabled;           // this physical port is in use
        uint16_t blockTimer;        // ticks remaining while blocked
        uint64_t txFreeAt;          // cycle at which the shifter is free
    };

    struct State
    {
        uint8_t    shared[kSharedSize];     // $EC00-$ECBF
        uint8_t    flags[kFlagBytes];       // per-byte access flags
        uint8_t    ramDir;                  // shared-RAM direction bits
        uint8_t    ipce[4];                 // $ECF8-$ECFB read side
        uint8_t    ipcm[4];                 // $ECF8-$ECFB write side
        uint8_t    semaphore;               // $ECFF
        uint8_t    p0dir;                   // $ECF7; P0/P1 themselves pass through
        uint8_t    running;                 // released from the boot handshake
        uint8_t    outOwner;                // arbitration lock: 0, 8 or 9
        uint8_t    hostIndex;               // cursor into $EC00-$EC0F, 0xff idle
        uint8_t    dStatus, dCount;         // source D message tracking
        uint8_t    dInSysex;
        uint8_t    hostRs[2];               // per host port running status
        uint8_t    asTxCount;               // ticks to the next Active Sensing byte
        uint8_t    rtsCount;                // COMPUTER-port flow control
        uint8_t    rtsAsserted;
        uint8_t    kickHostBuf;             // rescan $EC00-$EC0F from the start
        // COMPUTER input yields after a host delivery; new input or a resume command wakes it.
        uint8_t    wakeC;
        uint8_t    ringC[kRingC];           // mirror of $EC40-$EC9F
        uint8_t    rxCwr, rxCrd;
        MidiSource src[SRC_COUNT];
        SerialOut  out[2];                  // [0] = MIDI OUT, [1] = COMPUTER
        uint64_t   cycles;
        uint64_t   nextTick;
        uint64_t   rxFreeAt[3];             // per-input byte pacing
        // Queue arrivals separately from the receive buffers to model serial byte timing.
        uint8_t    inQueue[3][kInQueue];
        uint16_t   inWr[3], inRd[3];
    };

    Sc55SubMcu();

    void setHooks(const Hooks& h) { m_hooks = h; }

    // Reset and wait for the main MCU's boot handshake.
    void reset();

    // Advance to the main MCU's cycle count.
    void update(uint64_t mcuCycles);

    // The $EC00-$ECFF window.
    uint8_t hostRead(uint32_t address);
    void    hostWrite(uint32_t address, uint8_t data);

    // Feed the serial inputs.  Bytes are paced at the real baud rate, so
    // these queue rather than being consumed immediately.
    void postMidiIn(uint8_t data)   { pushInput(SRC_A, data); }
    void postMidiIn2(uint8_t data)  { pushInput(SRC_B, data); }
    void postComputer(uint8_t data) { pushInput(SRC_C, data); }

private:

    void    pushInput(int src, uint8_t data);
    void    serviceInputs();
    void    receiveByte(int src, uint8_t data);
    bool    fetch(int src, uint8_t& out);
    bool    fetchFromHostBuf(uint8_t& out);

    enum Put { PUT_SWALLOWED, PUT_SENT, PUT_DEFERRED };
    Put     putSerial(int port, uint8_t data, bool filtered);
    Put     putHost(int port, uint8_t data, bool filtered);
    void    deliver(uint8_t data, uint8_t& remaining, uint8_t& filtered);
    void    emitSerial(int port, uint8_t data);

    void     tick();
    void     pumpAll();
    void     pumpSource(int src);
    void     drainTx();
    uint8_t  destinationsFor(int src) const;
    bool     lockAvailable(int src) const;
    void     updateLock(int src);
    uint64_t computerByteCycles() const;
    void     setRts(bool asserted);

    void    raiseStatus(uint8_t bits);
    void    pulseIrq();
    void    handleCommand(uint8_t cmd);
    void    initFromConfig();
    void    reportErrors();

    bool    flagGet(uint8_t off) const
                { return (m_s.flags[off >> 3] >> (off & 7)) & 1; }
    void    flagSet(uint8_t off)
                { m_s.flags[off >> 3] |= (uint8_t)(1 << (off & 7)); }
    void    flagClear(uint8_t off)
                { m_s.flags[off >> 3] &= (uint8_t)~(1 << (off & 7)); }

    State m_s;
    Hooks m_hooks;
    bool  m_hostWrote;      // transient: a host port accepted the last byte
};

}	// namespace emu88Lib
