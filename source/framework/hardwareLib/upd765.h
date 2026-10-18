#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hwLib
{
	// NEC uPD765 / uPD72067 / uPD72068 floppy disk controller, at the level a firmware
	// talks to it: the command / result protocol on the FIFO port, the main
	// status register, the interrupt and DMA request lines, terminal count and
	// drive READY. Nothing below that - no MFM, no bit cells, no rotation. A
	// sector is delivered as its bytes, paced at the wire rate so the host's
	// DMA channel sees the same cadence it would from the cable.
	//
	// Modelled: SPECIFY, SENSE DRIVE STATUS, RECALIBRATE, SEEK, SENSE INTERRUPT
	// STATUS, READ ID and READ DATA (multi-sector up to EOT, multi-track from
	// side 0 to side 1, ended by TC), plus
	// the uPD7206x auxiliary commands on the status port. Of those only the
	// software reset and the data rate of "control internal mode" change
	// anything here. Everything else answers as an invalid command (ST0 0x80).
	// One drive.
	//
	// Time is the caller's: advance() takes elapsed CPU states and the
	// constructor is told how many of those make a microsecond, and the data
	// rate the controller starts at (500 kbit/s for 2HD, 250 kbit/s for 2DD).
	class Upd765
	{
	public:
		using LineCallback = std::function<void(bool)>;

		// What the controller reads sectors from.
		class Disk
		{
		public:
			virtual ~Disk() = default;
			virtual bool present() const = 0;
			// `_sector` is 1-based, as it is in the ID field.
			virtual bool readSector(uint8_t _cylinder, uint8_t _head, uint8_t _sector, uint8_t* _out, size_t _size) const = 0;
			virtual bool writeProtected() const { return true; }
		};

		enum Status : uint8_t
		{
			MsrDriveBusy0 = 0x01,
			MsrCommandBusy = 0x10,
			MsrExecMode = 0x20,
			MsrDataInput = 0x40,	// DIO: set = controller to host
			MsrRequest = 0x80,		// RQM
		};

		explicit Upd765(uint32_t _statesPerMicrosecond, uint32_t _bitsPerSecond = 500000);

		void setDisk(Disk* _disk) { m_disk = _disk; }
		void setIrqCallback(LineCallback _cb) { m_irqCallback = std::move(_cb); }
		void setDrqCallback(LineCallback _cb) { m_drqCallback = std::move(_cb); }
		// Fires when a SEEK or RECALIBRATE completes: the drive has been
		// stepped, which is what clears its disk-change flag.
		void setStepCallback(std::function<void()> _cb) { m_stepCallback = std::move(_cb); }

		// RESET pin.
		void reset();
		// The drive's READY input, true when a disk is in and the drive says so.
		void setReady(bool _ready) { m_ready = _ready; }

		uint8_t readStatus() const;
		void    writeAuxCommand(uint8_t _val);	// uPD7206x: writes to the status port
		uint8_t readFifo();
		void    writeFifo(uint8_t _val);

		// The DMA acknowledge: hand over the byte the DRQ announced.
		uint8_t dmaRead();
		// TC pin (active high here).
		void setTerminalCount(bool _tc);

		void advance(uint32_t _states);

		// MFM wire rate. "Control internal mode" sets it too.
		void setDataRate(uint32_t _bitsPerSecond);

		bool irq() const { return m_irq; }
		bool drq() const { return m_drq; }
		uint8_t presentCylinder() const { return m_pcn; }

		// Bring-up telemetry. The trace hook, when set, gets one line per
		// command (bytes in, bytes out) and one per status-port aux command.
		void setTraceHook(std::function<void(const std::string&)> _hook) { m_trace = std::move(_hook); }
		uint32_t commandsExecuted() const { return m_commandsExecuted; }
		uint32_t sectorsRead() const { return m_sectorsRead; }
		uint8_t  lastCommand() const { return m_lastCommand; }

	private:
		enum class Phase { Command, Execute, Result };
		enum class Pending { None, SeekDone };

		static constexpr uint32_t MaxSectorSize = 4096;

		void setIrq(bool _v);
		void setDrq(bool _v);
		void execute();
		void beginRead();
		bool loadSector();
		void nextSectorId();
		bool multiTrack() const { return (m_command[0] & 0x80) != 0; }
		void finishRead(bool _tc);
		void resultBytes(std::initializer_list<uint8_t> _bytes);

		Disk* m_disk = nullptr;
		LineCallback m_irqCallback, m_drqCallback;
		std::function<void()> m_stepCallback;
		const uint32_t m_statesPerUs;

		Phase m_phase = Phase::Command;
		std::array<uint8_t, 9> m_command {};
		uint32_t m_commandLength = 0;
		std::array<uint8_t, 7> m_result {};
		uint32_t m_resultLength = 0, m_resultPos = 0;

		bool m_irq = false, m_drq = false, m_tc = false, m_ready = false;
		bool m_nonDma = false;
		uint16_t m_specify = 0;
		uint8_t  m_pcn = 0;
		uint8_t  m_st0 = 0, m_st1 = 0, m_st2 = 0;
		bool     m_st0Filled = false;
		bool     m_driveBusy = false;

		// Seek / recalibrate completion.
		Pending  m_pending = Pending::None;
		uint64_t m_pendingAt = 0;
		uint8_t  m_seekTarget = 0;
		uint64_t m_now = 0;

		// Read in progress.
		std::array<uint8_t, MaxSectorSize> m_sector {};
		uint32_t m_sectorSize = 0, m_sectorPos = 0;
		uint32_t m_byteStates = 0, m_byteAccumulator = 0;
		uint8_t  m_dataByte = 0;

		uint32_t m_commandsExecuted = 0, m_sectorsRead = 0;
		uint8_t  m_lastCommand = 0;
		std::function<void(const std::string&)> m_trace;
		uint32_t m_traceCommandLength = 0;
		void trace() const;
	};
}
