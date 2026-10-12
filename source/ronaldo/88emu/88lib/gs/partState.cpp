#include "88lib/gs/partState.h"

#include "88lib/gs/gs.h"

#include <algorithm>

namespace emu88Lib::gs
{
	bool isPartEditorModel(const DeviceModel _model)
	{
		switch(_model)
		{
		case DeviceModel::Sc88:
		case DeviceModel::Sc88VL:
		case DeviceModel::Sc88Pro:
		case DeviceModel::Sc8850:
		case DeviceModel::Sc8820:
		case DeviceModel::Xpgs:
		case DeviceModel::VeGsPro:
			return true;
		default:
			return isSc55Model(_model);
		}
	}

	bool hasToneMaps(const DeviceModel _model)
	{
		return isPartEditorModel(_model) && !isSc55Model(_model);
	}

	bool hasRxBankSelect(const DeviceModel _model)
	{
		// The first SC-55 programs answer no request for it and go on taking bank select after GM System On
		switch(_model)
		{
		case DeviceModel::Sc55Mk1:
		case DeviceModel::Sc155:
		case DeviceModel::Cm300:
		case DeviceModel::Scc1a:
			return false;
		default:
			return isPartEditorModel(_model);
		}
	}

	bool hasKeepTone(const DeviceModel _model)
	{
		return _model == DeviceModel::Sc55St || hasToneMaps(_model);
	}

	namespace
	{
		// The map a part on map 0 plays from after a reset: the board's own
		uint8_t defaultToneMap0(const DeviceModel _model)
		{
			switch(_model)
			{
			case DeviceModel::Sc88:
			case DeviceModel::Sc88VL:
			case DeviceModel::Xpgs:
				return 2;
			case DeviceModel::Sc88Pro:
			case DeviceModel::VeGsPro:
				return 3;
			case DeviceModel::Sc8850:
			case DeviceModel::Sc8820:
				return 4;
			default:
				return 0;
			}
		}

		// 40 1x 25 after a reset: set on the boards that keep a part's tone by default
		uint8_t defaultKeepTone(const DeviceModel _model)
		{
			switch(_model)
			{
			case DeviceModel::Sc88Pro:
			case DeviceModel::VeGsPro:
			case DeviceModel::Sc8850:
			case DeviceModel::Sc8820:
				return 1;
			default:
				return 0;
			}
		}
	}

	bool Part::operator==(const Part& _p) const
	{
		return bankMsb == _p.bankMsb && program == _p.program && rxChannel == _p.rxChannel && rxNoteMessage == _p.rxNoteMessage &&
		       useForRhythm == _p.useForRhythm && level == _p.level && pan == _p.pan &&
		       rxBankSelect == _p.rxBankSelect && keepTone == _p.keepTone && toneMap == _p.toneMap &&
		       toneMap0 == _p.toneMap0 && pendingBankMsb == _p.pendingBankMsb && pendingBankLsb == _p.pendingBankLsb &&
		       pendingToneMap0 == _p.pendingToneMap0;
	}

	uint8_t Setup::effectiveToneMap(const size_t _part) const
	{
		if(_part >= parts.size())
			return 0;
		const auto& p = parts[_part];
		return p.toneMap ? p.toneMap : p.toneMap0;
	}

	Setup createDefaultSetup(const DeviceModel _model)
	{
		Setup setup;
		if(!isPartEditorModel(_model))
			return setup;

		const auto groups = std::max<uint8_t>(1, getDeviceProfile(_model).groupCount);
		setup.parts.resize(static_cast<size_t>(groups) * g_partsPerGroup);
		for(size_t i = 0; i < setup.parts.size(); ++i)
		{
			auto& p = setup.parts[i];
			const auto part = static_cast<uint8_t>(i % g_partsPerGroup);
			p.rxChannel = part;
			p.useForRhythm = part == 9 ? 1 : 0;
			p.toneMap0 = p.pendingToneMap0 = defaultToneMap0(_model);
			p.keepTone = defaultKeepTone(_model);
		}
		return setup;
	}

	PartStateTracker::PartStateTracker()
	{
		m_setup = createDefaultSetup(m_model);
	}

	void PartStateTracker::setModel(const DeviceModel _model, std::shared_ptr<const ToneCatalog> _catalog)
	{
		std::lock_guard lock(m_mutex);
		m_model = _model;
		m_catalog = std::move(_catalog);
		resetLocked();
	}

	DeviceModel PartStateTracker::model() const
	{
		std::lock_guard lock(m_mutex);
		return m_model;
	}

	void PartStateTracker::reset()
	{
		std::lock_guard lock(m_mutex);
		resetLocked();
	}

	void PartStateTracker::process(const synthLib::SMidiEvent& _event)
	{
		// Most of what a song sends are notes; those pass without taking the lock
		if(_event.sysex.empty())
		{
			const uint8_t type = _event.a & 0xf0;
			const bool controller = type == 0xb0 && (_event.b == 0 || _event.b == 32 || _event.b == 7 || _event.b == 10);
			if(!controller && type != 0xc0)
				return;
		}
		else if(_event.sysex.size() < 6 || _event.sysex[0] != 0xf0 ||
		        (_event.sysex[1] != g_rolandId && _event.sysex[1] != 0x7e && _event.sysex[1] != 0x7f))
		{
			return;
		}
		std::lock_guard lock(m_mutex);
		processLocked(_event, _event.port, false);
	}

	void PartStateTracker::processReply(const synthLib::SMidiEvent& _event, const uint8_t _port)
	{
		std::lock_guard lock(m_mutex);
		// The board answers on the port the request came in on, with 40 meaning that port's group again
		if(!_event.sysex.empty())
			processLocked(_event, _port, true);
	}

	Setup PartStateTracker::setup() const
	{
		std::lock_guard lock(m_mutex);
		return m_setup;
	}

	Setup PartStateTracker::setup(uint64_t& _revision) const
	{
		std::lock_guard lock(m_mutex);
		_revision = m_revision.load(std::memory_order_relaxed);
		return m_setup;
	}

	void PartStateTracker::processLocked(const synthLib::SMidiEvent& _event, const uint8_t _port, const bool _reply)
	{
		if(m_setup.parts.empty())
			return;
		const bool modified = _event.sysex.empty()
			? processChannelMessage(_event.a, _event.b, _event.c, _port)
			: processSysex(_event.sysex, _port, _reply);
		if(modified)
			changed();
	}

	void PartStateTracker::selectTone(Part& _part, const uint8_t _bankMsb, const uint8_t _program, const bool _sysex) const
	{
		const bool maps = hasToneMaps(m_model);

		// A tone number by SysEx: the first SC-55 programs store it as it comes, the later ones take it through the
		// program change. The SC-88 family applies the map a CC#32 left behind as a program change would, the
		// SC-8850 keeps the part's map.
		const bool sc8850 = m_model == DeviceModel::Sc8850 || m_model == DeviceModel::Sc8820;
		if(_sysex && !hasRxBankSelect(m_model) && m_model != DeviceModel::Scb55)
		{
			_part.bankMsb = _part.pendingBankMsb = _bankMsb;
			_part.program = _program;
			return;
		}
		// The SC-88Pro's SysEx tone number obeys Rx Bank Select. On the SC-88 family the bank it brings stays in the
		// CC#0 latch, whatever comes of it; the SC-8850 leaves the latch alone.
		const bool proGeneration = m_model == DeviceModel::Sc88Pro || m_model == DeviceModel::VeGsPro;
		const uint8_t bankMsb = _sysex && proGeneration && !_part.rxBankSelect ? 0 : _bankMsb;
		if(_sysex && !sc8850)
			_part.pendingBankMsb = bankMsb;

		// With Rx Bank Select off the map a CC#32 or a SysEx map left behind is not applied either
		const bool latched = !(_sysex && sc8850);
		const uint8_t lsb = !_part.rxBankSelect ? 0 : latched ? _part.pendingBankLsb : _part.toneMap;
		if(maps && latched)
			_part.toneMap0 = _part.pendingToneMap0;
		const uint8_t map = maps ? (lsb ? lsb : _part.toneMap0) : 0;

		auto selection = Selection::Tone;
		if(m_catalog)
		{
			if(_part.isDrumPart())
			{
				// The first SC-55 programs ignore a drum set selected with a bank; the SC-88 family a drum set
				// selected by SysEx with one
				if(bankMsb != 0 && (m_catalog->drumBankMustBeZero || (_sysex && maps && !sc8850)))
					return;
				selection = m_catalog->selectDrumSet(map, _program).selection;
			}
			// The SC-8850's GM2 banks play from a map the catalog does not list
			else if(!(sc8850 && bankMsb >= 120))
			{
				selection = m_catalog->selectTone(map, bankMsb, _program, _part.keepTone != 0).selection;
			}
		}

		if(selection == Selection::Ignored)
		{
			// The firmware puts the part back as it was. The SC-88 family puts the map latch back too, the SC-8850
			// leaves it for the next program change.
			if(maps && m_catalog->ignoreRestoresMap)
				_part.pendingBankLsb = _part.toneMap;
			return;
		}

		// A drum part plays the set whatever the variation, and reports none
		_part.bankMsb = _part.isDrumPart() ? 0 : bankMsb;
		_part.program = _program;
		if(maps)
			_part.toneMap = lsb;
	}

	bool PartStateTracker::processChannelMessage(const uint8_t _status, const uint8_t _data1, const uint8_t _data2,
		const uint8_t _port)
	{
		if(_status < 0x80 || _status >= 0xf0)
			return false;

		const auto first = static_cast<size_t>(_port) * g_partsPerGroup;
		if(first >= m_setup.parts.size())
			return false;

		const uint8_t channel = _status & 0x0f;
		const uint8_t type = _status & 0xf0;
		const bool maps = hasToneMaps(m_model);
		bool modified = false;

		// Every part of the port's group that listens to the channel
		for(size_t i = first; i < first + g_partsPerGroup; ++i)
		{
			auto& p = m_setup.parts[i];
			if(p.rxChannel != channel)
				continue;

			const auto before = p;
			if(type == 0xb0)
			{
				switch(_data1)
				{
				case 0: p.pendingBankMsb = _data2; break;
				case 32: if(maps) p.pendingBankLsb = _data2; break;
				case 7: p.level = _data2; break;
				case 10: p.pan = _data2; break;
				default: break;
				}
			}
			else if(type == 0xc0)
			{
				// With Rx Bank Select off a part takes bank select as 0, the map's too
				if(!p.rxBankSelect)
					p.pendingBankLsb = 0;
				selectTone(p, p.rxBankSelect ? p.pendingBankMsb : 0, _data1, false);
			}
			modified |= p != before;
		}
		return modified;
	}

	bool PartStateTracker::processSysex(const synthLib::SysexBuffer& _sysex, const uint8_t _port, const bool _reply)
	{
		const auto* s = _sysex.data();
		const auto size = _sysex.size();

		// GM System On, GM2 System On: the board starts over
		if(size == 6 && s[0] == 0xf0 && s[1] == 0x7e && s[3] == 0x09 && (s[4] == 0x01 || s[4] == 0x03))
		{
			resetLocked(s[4] == 0x01);
			return true;
		}

		// Universal realtime Master Volume, F0 7F dev 04 01 ll mm F7
		if(size == 8 && s[0] == 0xf0 && s[1] == 0x7f && s[3] == 0x04 && s[4] == 0x01)
		{
			const auto before = m_setup.masterVolume;
			m_setup.masterVolume = s[6] & 0x7f;
			return before != m_setup.masterVolume;
		}

		Dt1 dt1;
		if(!parseDt1(s, size, dt1))
			return false;

		if((dt1.address == g_gsReset && dt1.size >= 1 && dt1.data[0] == 0x00) || dt1.address == g_systemModeSet)
		{
			resetLocked();
			return true;
		}

		bool modified = false;
		ToneWrite toneWrite;
		for(size_t i = 0; i < dt1.size; ++i)
			modified |= writeAddress(addressAdd(dt1.address, static_cast<uint32_t>(i)), dt1.data[i], _port, _reply, toneWrite);

		if(auto* p = toneWrite.part)
		{
			const auto before = *p;
			selectTone(*p, static_cast<uint8_t>(toneWrite.bankMsb >= 0 ? toneWrite.bankMsb : p->bankMsb),
				static_cast<uint8_t>(toneWrite.program >= 0 ? toneWrite.program : p->program), true);
			modified |= *p != before;
		}
		return modified;
	}

	bool PartStateTracker::writeAddress(const uint32_t _address, const uint8_t _value, const uint8_t _port,
		const bool _reply, ToneWrite& _toneWrite)
	{
		const auto high = static_cast<uint8_t>(_address >> 16);
		const auto mid = static_cast<uint8_t>(_address >> 8);
		const auto low = static_cast<uint8_t>(_address);

		// 40 means the group of the port it arrives at. 50 is group B, from port A or B: the SC-8850 ignores it on
		// its ports C and D.
		uint8_t group;
		if(high == 0x40)
			group = _port;
		else if(high == 0x50 && _port <= 1)
			group = 1;
		else
			return false;

		if(mid == 0x00)
		{
			if(low != (g_masterVolume & 0xff))
				return false;
			const auto before = m_setup.masterVolume;
			m_setup.masterVolume = _value;
			return before != _value;
		}

		auto* p = partAt(group, mid & 0x0f);
		if(!p)
			return false;

		const auto before = *p;
		switch(mid & 0xf0)
		{
		case 0x10:
			switch(static_cast<PartParam>(low))
			{
			case PartParam::ToneNumber:
			case PartParam::Program:
				// A reply is what the board has; a message to it is a request the firmware may turn down
				if(_reply)
				{
					if(low == static_cast<uint8_t>(PartParam::ToneNumber))
						p->bankMsb = p->pendingBankMsb = _value;
					else
						p->program = _value;
					break;
				}
				if(_toneWrite.part && _toneWrite.part != p)
				{
					// A write across two parts' tone numbers: settle the first before the second
					selectTone(*_toneWrite.part,
						static_cast<uint8_t>(_toneWrite.bankMsb >= 0 ? _toneWrite.bankMsb : _toneWrite.part->bankMsb),
						static_cast<uint8_t>(_toneWrite.program >= 0 ? _toneWrite.program : _toneWrite.part->program), true);
					_toneWrite = {};
				}
				_toneWrite.part = p;
				(low == static_cast<uint8_t>(PartParam::ToneNumber) ? _toneWrite.bankMsb : _toneWrite.program) = _value;
				break;
			case PartParam::RxChannel: p->rxChannel = _value; break;
			case PartParam::RxNoteMessage: p->rxNoteMessage = _value; break;
			case PartParam::UseForRhythm:
				if(_reply)
				{
					p->useForRhythm = _value;
				}
				else if(_value <= 2 && _value != p->useForRhythm)
				{
					// A part that changes its kind starts over on program 1, bank 0; the SC-88Pro brings a normal part
					// back on the bank its CC#0 latch holds
					p->useForRhythm = _value;
					p->program = 0;
					p->bankMsb = !_value && (m_model == DeviceModel::Sc88Pro || m_model == DeviceModel::VeGsPro) ? p->pendingBankMsb : 0;
				}
				break;
			case PartParam::Level: p->level = _value; break;
			case PartParam::Pan: p->pan = _value; break;
			case PartParam::RxBankSelect: if(hasRxBankSelect(m_model)) p->rxBankSelect = _value; break;
			case PartParam::KeepTone: if(hasKeepTone(m_model)) p->keepTone = _value; break;
			default: break;
			}
			break;
		case 0x40:
			if(!hasToneMaps(m_model))
				break;
			// A map set by SysEx waits for the part's next program change, as a CC#32 does; the SC-88 family's
			// SysEx tone number takes it up as well
			if(low == static_cast<uint8_t>(PartMapParam::ToneMap))
			{
				p->pendingBankLsb = _value;
				if(_reply)
					p->toneMap = _value;
			}
			else if(low == static_cast<uint8_t>(PartMapParam::ToneMap0))
			{
				p->pendingToneMap0 = _value;
				if(_reply)
					p->toneMap0 = _value;
			}
			break;
		default:
			break;
		}
		return *p != before;
	}

	Part* PartStateTracker::partAt(const uint8_t _group, const uint8_t _block)
	{
		const auto index = static_cast<size_t>(_group) * g_partsPerGroup + blockPart(_block);
		return index < m_setup.parts.size() ? &m_setup.parts[index] : nullptr;
	}

	void PartStateTracker::resetLocked(const bool _gmSystemOn)
	{
		m_setup = createDefaultSetup(m_model);
		// GM System On turns bank select off on every part of the boards that have the switch
		if(_gmSystemOn && hasRxBankSelect(m_model))
		{
			for(auto& p : m_setup.parts)
				p.rxBankSelect = 0;
		}
		changed();
	}

	void PartStateTracker::changed()
	{
		m_revision.fetch_add(1, std::memory_order_acq_rel);
	}
}
