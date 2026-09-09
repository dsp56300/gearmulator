// CPU-side registers of the H8/570 ISP (intelligent sub-processor), the
// on-chip bus-mastering microengine (hardware manual appendix B).
//
// The ISP's program is not emulated; its services are modelled by the board
// (glue/isp).  This device holds the contract both sides share:
//   H'FEB0/1  ISFH/ISFL   interrupt status flags, sticky; the ISP sets them,
//                         the CPU clears them by writing 0.  Delivery to the
//                         CPU is ISF & IEF (ISF0-15 -> IrqSrc::Isf0-15).
//   H'FEB2-4  IOF2/1/0    I/O flags (read-only pin images)
//   H'FEB5    EGF         edge detection flags
//   H'FEB6/7  ICFH/ICFL   interconnection flags (CPU <-> ISP handshake bits;
//                         the PWM sets ICFH6/7, the CPU claims ICFL0)
//   H'FEB8/9  IEFH/IEFL   interrupt enable flags
//   H'FEBA/B  IOIEH/IOIEL I/O interrupt enables
//   H'FEBC/D  CLEH/CLEL   ICF read-clear enables
//   H'FEBF    EVER        event input enables
//   H'FF18    IPR         ISP memory-access page register
//   H'FF19    ICSR        ISP control/status (IRST reset hold, function restart)
//   H'FF28/9  FEDGE/REDGE input edge selects
// DR0-DR31 (H'FEC0-H'FEFF) belong to the ISP model itself.
#pragma once
#include <functional>

#include "cpu/h8500/bus.hpp"
#include "cpu/h8500/intc.hpp"
#include "common/iomux.hpp"

namespace h8500 {

class IspRegs final : public Device {
 public:
  explicit IspRegs(Intc& intc);

  void map(emu::IoMux& mux);
  void reset();

  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;

  // --- ISP side --------------------------------------------------------------
  void raise_isf(unsigned n, bool level = true);
  bool isf_pending(unsigned n) const { return (isf_ >> (n & 15)) & 1; }
  void set_icf(unsigned n, bool level);       // n = 0..15 (ICFL0..7, ICFH0..7)
  bool icf(unsigned n) const { return (icf_ >> (n & 15)) & 1; }
  bool isp_reset_held() const { return (icsr_ & 0x20) != 0; }
  u8 ipr() const { return ipr_; }

  // The CPU acknowledged ISFn (wrote its bit from 1 to 0).
  void set_isf_clear_hook(std::function<void(unsigned)> hook) { isf_clear_hook_ = std::move(hook); }
  // ICSR.IRST changed (true = the ISP is held in reset).
  void set_reset_hook(std::function<void(bool)> hook) { reset_hook_ = std::move(hook); }

  u16 isf() const { return isf_; }
  u16 ief() const { return ief_; }

 private:
  void update_delivery();

  Intc& intc_;
  u16 isf_ = 0, iof_ = 0, icf_ = 0, ief_ = 0, ioie_ = 0, cle_ = 0;
  u8 iof0_ = 0, egf_ = 0, ever_ = 0;
  u8 ipr_ = 0, icsr_ = 0x20, fedge_ = 0, redge_ = 0;
  std::function<void(unsigned)> isf_clear_hook_;
  std::function<void(bool)> reset_hook_;
};

}  // namespace h8500
