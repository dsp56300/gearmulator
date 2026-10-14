#include "upd765.h"

#include <algorithm>
#include <cstdio>

namespace hwLib
{
	namespace
	{
		enum St0 : uint8_t { St0HeadBit = 0x04, St0NotReady = 0x08, St0SeekEnd = 0x20, St0Fail = 0x40, St0Invalid = 0x80 };
		enum St1 : uint8_t { St1MissingAddress = 0x01, St1NoData = 0x04, St1EndOfCylinder = 0x80 };
		enum St2 : uint8_t { St2WrongCylinder = 0x10 };
		enum St3 : uint8_t { St3TwoSide = 0x08, St3Track0 = 0x10, St3Ready = 0x20, St3WriteProtected = 0x40 };

		// A step and its settle. Generous rather than exact; the firmware waits
		// on the interrupt either way.
		constexpr uint32_t g_seekMicroseconds = 2000;

		// Command byte count by opcode, 0 for unknown.
		uint32_t commandLength(const uint8_t _op)
		{
			switch (_op & 0x1f)
			{
			case 0x03: return 3;	// SPECIFY
			case 0x04: return 2;	// SENSE DRIVE STATUS
			case 0x07: return 2;	// RECALIBRATE
			case 0x08: return 1;	// SENSE INTERRUPT STATUS
			case 0x0f: return 3;	// SEEK
			case 0x0a: return 2;	// READ ID
			case 0x06: return 9;	// READ DATA
			case 0x0c: return 9;	// READ DELETED DATA (treated as READ DATA)
			default:   return 1;	// invalid: answered after the opcode byte
			}
		}
	}

	Upd765::Upd765(const uint32_t _statesPerMicrosecond, const uint32_t _bitsPerSecond)
		: m_statesPerUs(_statesPerMicrosecond)
	{
		reset();
		setDataRate(_bitsPerSecond);
	}

	void Upd765::setDataRate(const uint32_t _bitsPerSecond)
	{
		// One MFM byte is eight bit cells: 16 us at 500 kbit/s, 32 us at 250.
		m_byteStates = static_cast<uint32_t>(uint64_t(m_statesPerUs) * 8000000 / _bitsPerSecond);
	}

	void Upd765::reset()
	{
		m_phase = Phase::Command;
		m_commandLength = 0;
		m_resultLength = m_resultPos = 0;
		m_tc = false;
		m_nonDma = false;
		m_pcn = 0;
		m_st0 = m_st1 = m_st2 = 0;
		m_st0Filled = false;
		m_driveBusy = false;
		m_pending = Pending::None;
		m_sectorSize = m_sectorPos = 0;
		m_byteAccumulator = 0;
		setIrq(false);
		setDrq(false);
	}

	void Upd765::setIrq(const bool _v)
	{
		if (m_irq == _v)
			return;
		m_irq = _v;
		if (m_irqCallback)
			m_irqCallback(_v);
	}

	void Upd765::setDrq(const bool _v)
	{
		if (m_drq == _v)
			return;
		m_drq = _v;
		if (m_drqCallback)
			m_drqCallback(_v);
	}

	uint8_t Upd765::readStatus() const
	{
		uint8_t msr = m_driveBusy ? MsrDriveBusy0 : 0;
		switch (m_phase)
		{
		case Phase::Command:
			msr |= MsrRequest;
			if (m_commandLength)
				msr |= MsrCommandBusy;
			break;
		case Phase::Execute:
			msr |= MsrCommandBusy;
			if (m_nonDma)
				msr |= MsrExecMode;
			if (m_drq && m_nonDma)
				msr |= MsrRequest | MsrDataInput;
			break;
		case Phase::Result:
			msr |= MsrRequest | MsrDataInput | MsrCommandBusy;
			break;
		}
		return msr;
	}

	void Upd765::writeAuxCommand(const uint8_t _val)
	{
		// uPD7206x auxiliary commands, written to the status port. 0x36 is a
		// software reset. The clock start / external mode / internal mode /
		// motor / format family answers with a one-byte result phase - the
		// S-760 boot ROM issues 0x27 and spins until MSR shows command busy,
		// then collects that byte. Of these only "control internal mode"
		// changes anything this model distinguishes: bits 7-6 pick the data
		// rate on the uPD72068.
		if (m_trace)
		{
			char buf[32];
			std::snprintf(buf, sizeof(buf), "fdc aux %02x", _val);
			m_trace(buf);
		}
		m_traceCommandLength = 0;
		switch (_val)
		{
		case 0x36:
			reset();
			return;
		case 0x34:	// reset standby
		case 0x35:	// set standby
			return;
		default:
			break;
		}
		switch (_val & 0x0f)
		{
		case 0x03:	// enable external mode
		case 0x07:	// start clock
		case 0x0b:	// control internal mode (data rate in bits 7-6)
			{
				static constexpr uint32_t rates[] = {250000, 500000, 600000, 300000};
				setDataRate(rates[_val >> 6]);
			}
			resultBytes({St0Invalid});
			break;
		case 0x0e:	// enable motors
		case 0x0f:	// select format
			resultBytes({St0Invalid});	// "successful": the same 0x80 the reference reports
			break;
		default:
			break;
		}
	}

	uint8_t Upd765::readFifo()
	{
		setIrq(false);
		switch (m_phase)
		{
		case Phase::Result:
			{
				const uint8_t v = m_result[m_resultPos++];
				if (m_resultPos >= m_resultLength)
				{
					m_phase = Phase::Command;
					m_resultLength = m_resultPos = 0;
				}
				return v;
			}
		case Phase::Execute:
			if (m_drq && m_nonDma)
				return dmaRead();
			return 0xff;
		default:
			return 0xff;
		}
	}

	void Upd765::writeFifo(const uint8_t _val)
	{
		setIrq(false);
		if (m_phase != Phase::Command)
			return;

		m_command[m_commandLength++] = _val;
		if (m_commandLength < commandLength(m_command[0]))
			return;
		if (m_commandLength >= m_command.size() && m_commandLength < commandLength(m_command[0]))
			return;
		execute();
		m_commandLength = 0;
	}

	void Upd765::resultBytes(const std::initializer_list<uint8_t> _bytes)
	{
		m_resultLength = 0;
		for (const uint8_t b : _bytes)
			m_result[m_resultLength++] = b;
		m_resultPos = 0;
		m_phase = Phase::Result;
		trace();
	}

	void Upd765::trace() const
	{
		if (!m_trace)
			return;
		std::string line = "fdc cmd";
		char buf[8];
		for (uint32_t i = 0; i < m_traceCommandLength; ++i) { std::snprintf(buf, sizeof(buf), " %02x", m_command[i]); line += buf; }
		line += " ->";
		for (uint32_t i = 0; i < m_resultLength; ++i) { std::snprintf(buf, sizeof(buf), " %02x", m_result[i]); line += buf; }
		std::snprintf(buf, sizeof(buf), " pcn=%u", m_pcn); line += buf;
		m_trace(line);
	}

	void Upd765::execute()
	{
		++m_commandsExecuted;
		m_lastCommand = m_command[0];
		m_traceCommandLength = m_commandLength;
		const uint8_t unit = m_command[1] & 0x07;	// hds + ds bits, as ST0 reports them

		switch (m_command[0] & 0x1f)
		{
		case 0x03:	// SPECIFY
			m_specify = static_cast<uint16_t>((m_command[1] << 8) | m_command[2]);
			m_nonDma = (m_command[2] & 1) != 0;
			m_phase = Phase::Command;
			m_resultLength = 0;
			trace();
			break;

		case 0x04:	// SENSE DRIVE STATUS
			{
				uint8_t st3 = unit | St3TwoSide;
				if (m_ready) st3 |= St3Ready;
				if (m_pcn == 0) st3 |= St3Track0;
				if (m_disk && m_disk->writeProtected()) st3 |= St3WriteProtected;
				resultBytes({st3});
			}
			break;

		case 0x07:	// RECALIBRATE
		case 0x0f:	// SEEK
			m_st0 = unit;
			if (!m_ready)
			{
				m_st0 |= St0NotReady | St0Fail | St0SeekEnd;
				m_st0Filled = true;
				m_phase = Phase::Command;
				setIrq(true);
				break;
			}
			m_seekTarget = (m_command[0] & 0x1f) == 0x07 ? 0 : m_command[2];
			m_driveBusy = true;
			m_pending = Pending::SeekDone;
			m_pendingAt = m_now + uint64_t(g_seekMicroseconds) * m_statesPerUs;
			m_phase = Phase::Command;
			m_resultLength = 0;
			trace();
			break;

		case 0x08:	// SENSE INTERRUPT STATUS
			if (m_st0Filled)
			{
				m_st0Filled = false;
				m_driveBusy = false;
				resultBytes({m_st0, m_pcn});
			}
			else
				resultBytes({St0Invalid});
			break;

		case 0x0a:	// READ ID
			m_st0 = unit;
			if (!m_ready)
			{
				m_st0 |= St0NotReady | St0Fail;
				resultBytes({m_st0, 0, 0, m_pcn, static_cast<uint8_t>((unit >> 2) & 1), 1, 2});
			}
			else
				resultBytes({m_st0, 0, 0, m_pcn, static_cast<uint8_t>((unit >> 2) & 1), 1, 2});
			setIrq(true);
			break;

		case 0x06:	// READ DATA
		case 0x0c:	// READ DELETED DATA
			beginRead();
			break;

		default:
			resultBytes({St0Invalid});
			break;
		}
	}

	void Upd765::beginRead()
	{
		// Command bytes: 1 hds/ds, 2 C, 3 H, 4 R, 5 N, 6 EOT, 7 GPL, 8 DTL.
		m_st0 = m_command[1] & 0x07;
		m_st1 = 0;
		m_st2 = 0;
		m_tc = false;

		if (!m_ready || !m_disk || !m_disk->present())
		{
			m_st0 |= St0NotReady | St0Fail;
			finishRead(false);
			return;
		}
		if (m_command[2] != m_pcn)
		{
			// The head is not where the ID says: no matching sector.
			m_st0 |= St0Fail;
			m_st1 |= St1NoData;
			m_st2 |= St2WrongCylinder;
			finishRead(false);
			return;
		}

		m_sectorSize = std::min<uint32_t>(128u << (m_command[5] & 7), MaxSectorSize);
		if (!loadSector())
		{
			m_st0 |= St0Fail;
			m_st1 |= St1NoData;
			finishRead(false);
			return;
		}
		m_phase = Phase::Execute;
		m_byteAccumulator = 0;
	}

	void Upd765::nextSectorId()
	{
		// The ID after a completed sector, as the result phase reports it: past EOT the
		// next cylinder - or, multi-track, the other side first.
		if (m_command[4] != m_command[6])
		{
			++m_command[4];
			return;
		}
		m_command[4] = 1;
		if (multiTrack() && (m_command[3] & 1) == 0)
		{
			m_command[3] |= 1;
			m_command[1] |= 0x04;
			return;
		}
		if (multiTrack())
		{
			m_command[3] &= static_cast<uint8_t>(~1);
			m_command[1] &= static_cast<uint8_t>(~0x04);
		}
		++m_command[2];
	}

	bool Upd765::loadSector()
	{
		m_sectorPos = 0;
		const uint8_t head = (m_command[3] & 1);
		if (!m_disk->readSector(m_command[2], head, m_command[4], m_sector.data(), m_sectorSize))
			return false;
		++m_sectorsRead;
		return true;
	}

	void Upd765::finishRead(const bool _tc)
	{
		// Result: ST0 ST1 ST2 C H R N. A read that ran into EOT without the
		// host ending it reports end-of-cylinder, the way the real part does.
		if (!_tc && (m_st0 & St0Fail) == 0 && m_command[4] == m_command[6])
		{
			m_st0 |= St0Fail;
			m_st1 |= St1EndOfCylinder;
		}
		setDrq(false);
		resultBytes({m_st0, m_st1, m_st2, m_command[2], m_command[3], m_command[4], m_command[5]});
		setIrq(true);
	}

	uint8_t Upd765::dmaRead()
	{
		if (!m_drq)
			return 0xff;
		setDrq(false);
		return m_dataByte;
	}

	void Upd765::setTerminalCount(const bool _tc)
	{
		const bool rising = !m_tc && _tc;
		m_tc = _tc;
		if (rising && m_phase == Phase::Execute)
		{
			// Ended by the host. If the current sector was consumed in full the
			// ID reported is the next one, as it would be after a completed
			// sector.
			if (m_sectorPos >= m_sectorSize)
				nextSectorId();
			finishRead(true);
		}
	}

	void Upd765::advance(const uint32_t _states)
	{
		m_now += _states;

		if (m_pending == Pending::SeekDone && m_now >= m_pendingAt)
		{
			m_pending = Pending::None;
			m_pcn = m_seekTarget;
			m_st0 |= St0SeekEnd;
			m_st0Filled = true;
			if (m_stepCallback)
				m_stepCallback();
			setIrq(true);
		}

		if (m_phase != Phase::Execute)
			return;

		// Deliver every byte whose wire time has elapsed, as long as the host
		// keeps taking them. A byte the host has not collected holds the line.
		m_byteAccumulator += _states;
		while (m_byteAccumulator >= m_byteStates && !m_drq && m_phase == Phase::Execute)
		{
			m_byteAccumulator -= m_byteStates;
			if (m_sectorPos >= m_sectorSize)
			{
				// Sector done. Continue with the next one until EOT - and with
				// multi-track set, from EOT on side 0 to sector 1 of side 1.
				if (m_command[4] == m_command[6] && !(multiTrack() && (m_command[3] & 1) == 0))
				{
					finishRead(false);
					return;
				}
				nextSectorId();
				if (!loadSector())
				{
					m_st0 |= St0Fail;
					m_st1 |= St1NoData;
					finishRead(false);
					return;
				}
			}
			m_dataByte = m_sector[m_sectorPos++];
			setDrq(true);
		}
	}
}
