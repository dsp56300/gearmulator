#include "88lib/gs/partState.h"

#include "88lib/gs/efx.h"
#include "88lib/gs/gs.h"

#include <algorithm>
#include <iterator>

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

	bool answersDataRequests(const DeviceModel _model)
	{
		return isPartEditorModel(_model) && _model != DeviceModel::VeGsPro;
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

		// The parameters the tracker has rules of its own for
		struct Indices
		{
			int masterVolume = findGlobalParamById("MasterVolume");
			int masterTune = findGlobalParamById("MasterTune");
			int efxType = findGlobalParamById("EfxType");
			int efxParam1 = findGlobalParamById("EfxParam1");
			int efxToReverb = findGlobalParamById("EfxToReverb");
			int expression = findPartParamById("Expression");
			int fineTune = findPartParamById("FineTune");
			int monoPoly = findPartParamById("MonoPoly");
			int partMode = findPartParamById("PartMode");
			int rxChannel = findPartParamById("RxChannel");
			int rxRpn = findPartParamById("RxRpn");
			int rxNrpn = findPartParamById("RxNrpn");
		};

		const Indices& indices()
		{
			static const Indices i;
			return i;
		}

		// Rx NRPN (40 1x 0A) after power-on, read back from the boards: off on these until a GS Reset. The SC-55st
		// is taken for an SC-55mkII, the CM-300 for the SCC-1A and the VE-GS Pro for the SC-88Pro, untested.
		bool nrpnOffAtPowerOn(const DeviceModel _model)
		{
			switch(_model)
			{
			case DeviceModel::Sc55Mk2:
			case DeviceModel::Sc55St:
			case DeviceModel::Scb55:
			case DeviceModel::Sc155Mk2:
			case DeviceModel::Scc1a:
			case DeviceModel::Cm300:
			case DeviceModel::Sc88:
			case DeviceModel::Sc88VL:
			case DeviceModel::Xpgs:
			case DeviceModel::Sc88Pro:
			case DeviceModel::VeGsPro:
				return true;
			default:
				return false;
			}
		}


		bool isTrackedController(const uint8_t _cc)
		{
			switch(_cc)
			{
			case 0: case 6: case 7: case 10: case 11: case 32: case 38: case 91: case 93: case 94:
			case 98: case 99: case 100: case 101: case 121: case 126: case 127:
				return true;
			default:
				return false;
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
		       pendingToneMap0 == _p.pendingToneMap0 && values == _p.values && fineTuneLsb == _p.fineTuneLsb;
	}

	uint16_t Part::value(const size_t _param) const
	{
		const auto& params = partParams();
		if(_param >= params.size())
			return 0;
		switch(params[_param].field)
		{
		case PartField::Level: return level;
		case PartField::Pan: return pan;
		case PartField::RxChannel: return rxChannel;
		case PartField::UseForRhythm: return useForRhythm;
		default: return values[_param];
		}
	}

	void Part::setValue(const size_t _param, const uint16_t _value)
	{
		const auto& params = partParams();
		if(_param >= params.size())
			return;
		const auto v = static_cast<uint8_t>(_value);
		switch(params[_param].field)
		{
		case PartField::Level: level = v; break;
		case PartField::Pan: pan = v; break;
		case PartField::RxChannel: rxChannel = v; break;
		case PartField::UseForRhythm: useForRhythm = v; break;
		default: values[_param] = v; break;
		}
	}

	uint16_t Setup::global(const char* _id) const
	{
		const auto param = findGlobalParamById(_id);
		return param >= 0 ? globals[static_cast<size_t>(param)] : 0;
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
		const auto& globals = globalParams();
		for(size_t i = 0; i < globals.size(); ++i)
			setup.globals[i] = globals[i].def;
		if(!isPartEditorModel(_model))
			return setup;

		const auto& params = partParams();

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
			for(size_t v = 0; v < params.size(); ++v)
			{
				if(params[v].field == PartField::None)
					p.values[v] = static_cast<uint8_t>(params[v].def);
			}
		}
		return setup;
	}

	PartStateTracker::PartStateTracker()
	{
		resetLocked(Reset::PowerOn);
	}

	void PartStateTracker::setModel(const DeviceModel _model, std::shared_ptr<const ToneCatalog> _catalog)
	{
		std::lock_guard lock(m_mutex);
		m_model = _model;
		m_catalog = std::move(_catalog);
		resetLocked(Reset::PowerOn);
	}

	DeviceModel PartStateTracker::model() const
	{
		std::lock_guard lock(m_mutex);
		return m_model;
	}

	void PartStateTracker::reset()
	{
		std::lock_guard lock(m_mutex);
		resetLocked(Reset::PowerOn);
	}

	void PartStateTracker::process(const synthLib::SMidiEvent& _event)
	{
		// Most of what a song sends are notes; those pass without taking the lock
		if(_event.sysex.empty())
		{
			m_activity.process(_event.a, _event.port);
			const uint8_t type = _event.a & 0xf0;
			if(!(type == 0xb0 && isTrackedController(_event.b)) && type != 0xc0)
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

		// Reset All Controllers only with value 0, but on the SC-8850
		if(type == 0xb0 && _data1 == 121 && _data2 && generation(m_model) != Generation::Sc8850)
			return false;

		// The board keeps the RPN and the NRPN apart, and a data entry goes to the kind selected last; on the SC-8850
		// an NRPN MSB of 7F does not count. Neither is selected after Reset All Controllers.
		const bool sc8850 = generation(m_model) == Generation::Sc8850;
		const auto select = [&](RpnSelection& _selection)
		{
			switch(_data1)
			{
			case 98: _selection.nrpnLsb = _data2; _selection.nrpnSelected = true; break;
			case 99: _selection.nrpnMsb = _data2; _selection.nrpnSelected |= !(sc8850 && _data2 == 0x7f); break;
			case 100: _selection.rpnLsb = _data2; _selection.nrpnSelected = false; break;
			case 101: _selection.rpnMsb = _data2; _selection.nrpnSelected = false; break;
			case 121: _selection.rpnMsb = _selection.rpnLsb = _selection.nrpnMsb = _selection.nrpnLsb = 0x7f; break;
			default: break;
			}
		};
		const bool perChannel = selectionPerChannel();
		const bool selecting = type == 0xb0 && ((_data1 >= 98 && _data1 <= 101) || _data1 == 121);
		if(selecting && perChannel)
			select(m_selections[first + channel]);
		// The SC-8850 keeps a channel's last data entry MSB in RPN mode, whichever RPN it went to: a data entry LSB
		// writes the fine tune with it
		if(perChannel && type == 0xb0 && _data1 == 6 && !m_selections[first + channel].nrpnSelected)
			m_selections[first + channel].fineTuneEntryMsb = _data2;

		// Every part of the port's group that listens to the channel
		for(size_t i = first; i < first + g_partsPerGroup; ++i)
		{
			auto& p = m_setup.parts[i];
			if(p.rxChannel != channel)
				continue;

			const auto before = p;
			auto& selection = m_selections[perChannel ? first + channel : i];
			if(selecting && !perChannel)
				select(selection);
			if(type == 0xb0)
			{
				switch(_data1)
				{
				case 0: p.pendingBankMsb = _data2; break;
				case 32: if(maps) p.pendingBankLsb = _data2; break;
				case 6: dataEntry(p, selection, _data2, false); break;
				case 38: dataEntry(p, selection, _data2, true); break;
				case 98: case 99: case 100: case 101: break;
				// Reset All Controllers brings the expression back up
				case 121: p.setValue(static_cast<size_t>(indices().expression), 127); break;
				// Mono with a channel count up to 16, poly with value 0
				case 126: if(_data2 <= 16) writePart(p, static_cast<size_t>(indices().monoPoly), 0, false); break;
				case 127: if(!_data2) writePart(p, static_cast<size_t>(indices().monoPoly), 1, false); break;
				default:
					if(const auto param = findPartParamByController(_data1); param >= 0)
						writePart(p, static_cast<size_t>(param), _data2, false);
					break;
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
			resetLocked(s[4] == 0x01 ? Reset::GmSystemOn : Reset::GsReset);
			return true;
		}

		// Universal realtime Master Volume, F0 7F dev 04 01 ll mm F7, which the first SC-55 programs do not know
		if(size == 8 && s[0] == 0xf0 && s[1] == 0x7f && s[3] == 0x04 && s[4] == 0x01)
		{
			if(isFirstSc55Program(m_model))
				return false;
			return writeGlobal(static_cast<size_t>(indices().masterVolume), s[6] & 0x7f, false);
		}

		Dt1 dt1;
		if(!parseDt1(s, size, dt1))
			return false;

		if((dt1.address == g_gsReset && dt1.size >= 1 && dt1.data[0] == 0x00) || dt1.address == g_systemModeSet)
		{
			resetLocked(Reset::GsReset);
			return true;
		}

		// The SC-8850 drops a message that starts at the insertion effect type's LSB; the SC-88Pro only that byte
		if((dt1.address & 0xffff) == 0x0301 && generation(m_model) == Generation::Sc8850)
			return false;

		bool modified = false;
		ToneWrite toneWrite;
		for(size_t i = 0; i < dt1.size; ++i)
		{
			const auto address = addressAdd(dt1.address, static_cast<uint32_t>(i));
			// The SC-8850 takes the chorus send to delay only from a message that starts there
			if(i && (address & 0xffff) == 0x0140 && generation(m_model) == Generation::Sc8850)
				continue;
			uint8_t offset = 0;
			if(const auto bytes = multiByteParam(address, offset))
			{
				// The board takes one from its first byte with all of them, and ignores a part of one
				if(offset == 0 && i + bytes <= dt1.size)
				{
					modified |= writeMultiByte(address, dt1.data + i, _port, _reply);
					i += bytes - 1u;
				}
				continue;
			}
			modified |= writeAddress(address, dt1.data[i], _port, _reply, toneWrite);
		}

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

		// The system, effect and equalizer parameters
		if(mid < 0x10)
		{
			if(!acceptsGlobal(high, mid, _port))
				return false;
			const auto param = findGlobalParam(0x400000u | static_cast<uint32_t>(mid) << 8 | low);
			return param >= 0 && globalParams()[static_cast<size_t>(param)].size == 1 &&
			       writeGlobal(static_cast<size_t>(param), _value, _reply);
		}

		uint8_t group;
		if(!targetGroup(high, _port, group))
			return false;

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
			case PartParam::RxChannel:
				// A part that changes its channel forgets the RPN and NRPN it had selected; the SC-55 and SC-155 with
				// any write of the channel
				if(!_reply && !selectionPerChannel())
				{
					uint16_t channel = _value;
					acceptValue(partParams()[static_cast<size_t>(indices().rxChannel)], m_model, channel);
					if(channel != p->rxChannel || m_model == DeviceModel::Sc55Mk1 || m_model == DeviceModel::Sc155)
						m_selections[static_cast<size_t>(p - m_setup.parts.data())] = RpnSelection();
				}
				writeTableParam(*p, 0x401000u | low, _value, _reply);
				break;
			case PartParam::RxNoteMessage: p->rxNoteMessage = _value; break;
			case PartParam::UseForRhythm:
				if(_reply)
				{
					p->useForRhythm = _value;
					break;
				}
				// The SC-8850 takes 3 as drum map 1 and ignores more, the others take the last one
				if(uint16_t kind = _value == 3 && generation(m_model) == Generation::Sc8850 ? 1 : _value;
					acceptValue(partParams()[static_cast<size_t>(indices().partMode)], m_model, kind) &&
					kind != p->useForRhythm)
				{
					// A part that changes its kind starts over on program 1, bank 0; the SC-88Pro brings a normal part
					// back on the bank its CC#0 latch holds
					p->useForRhythm = static_cast<uint8_t>(kind);
					p->program = 0;
					p->bankMsb = !kind && (m_model == DeviceModel::Sc88Pro
						|| m_model == DeviceModel::VeGsPro) ? p->pendingBankMsb : 0;
				}
				break;
			case PartParam::RxBankSelect: if(hasRxBankSelect(m_model)) p->rxBankSelect = _value; break;
			case PartParam::KeepTone: if(hasKeepTone(m_model)) p->keepTone = _value; break;
			default:
				writeTableParam(*p, 0x401000u | low, _value, _reply);
				break;
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
			else
			{
				writeTableParam(*p, 0x404000u | low, _value, _reply);
			}
			break;
		default:
			break;
		}
		return *p != before;
	}

	bool PartStateTracker::writeTableParam(Part& _part, const uint32_t _address, const uint8_t _value,
		const bool _reply)
	{
		const auto param = findPartParam(_address);
		if(param < 0)
			return false;
		const auto& desc = partParams()[static_cast<size_t>(param)];
		return desc.size == 1 && hasAddress(desc, m_model) && writePart(_part, static_cast<size_t>(param), _value,
			_reply);
	}

	uint8_t PartStateTracker::multiByteParam(const uint32_t _address, uint8_t& _offset) const
	{
		const auto mid = static_cast<uint8_t>(_address >> 8);
		const auto low = static_cast<uint8_t>(_address);
		if(mid == 0x00 && low < 4)
		{
			_offset = low;
			return 4;
		}
		if(mid == 0x03 && low < 2 && generation(m_model) >= Generation::Sc88Pro)
		{
			_offset = low;
			return 2;
		}
		if((mid & 0xf0) == 0x10 && (low == 0x2a || low == 0x2b) && generation(m_model) >= Generation::Sc88)
		{
			_offset = static_cast<uint8_t>(low - 0x2a);
			return 2;
		}
		return 0;
	}

	bool PartStateTracker::writeMultiByte(const uint32_t _address, const uint8_t* _data, const uint8_t _port,
		const bool _reply)
	{
		const auto high = static_cast<uint8_t>(_address >> 16);
		const auto mid = static_cast<uint8_t>(_address >> 8);
		const auto& idx = indices();

		if(mid == 0x00)
		{
			if(!acceptsGlobal(high, mid, _port))
				return false;
			const auto tune = static_cast<uint16_t>((_data[0] & 0x0f) << 12 | (_data[1] & 0x0f) << 8 |
				(_data[2] & 0x0f) << 4 | (_data[3] & 0x0f));
			return writeGlobal(static_cast<size_t>(idx.masterTune), tune, _reply);
		}
		if(mid == 0x03)
			return acceptsGlobal(high, mid, _port) && selectEfxType(static_cast<uint16_t>(_data[0] << 8 | _data[1]),
				_reply);

		uint8_t group;
		if(!targetGroup(high, _port, group))
			return false;
		auto* p = partAt(group, mid & 0x0f);
		if(!p)
			return false;
		const auto before = *p;
		if(writePart(*p, static_cast<size_t>(idx.fineTune), _data[0], _reply))
			p->fineTuneLsb = _data[1];
		return *p != before;
	}

	bool PartStateTracker::targetGroup(const uint8_t _high, const uint8_t _port, uint8_t& _group) const
	{
		// 40 means the group of the port it arrives at. 50 is group B, from port A or B: the SC-8850 ignores it on
		// its ports C and D.
		if(_high == 0x40)
			_group = _port;
		else if(_high == 0x50 && _port <= 1)
			_group = 1;
		else
			return false;
		return true;
	}

	bool PartStateTracker::acceptsGlobal(const uint8_t _high, const uint8_t _mid, const uint8_t _port) const
	{
		// The insertion effect from any port, and as 50 from ports A and B, on every board that has it
		if(_mid == 0x03)
			return _high == 0x40 || (_high == 0x50 && _port <= 1);

		// The SC-88, SC-88VL and SC-88Pro take the system and effect parameters on their first port only, the XP-GS
		// and the SC-8850 on any, the SC-8850 as 50 from ports A and B too. The SC-55s have one input.
		const bool sc8850 = generation(m_model) == Generation::Sc8850;
		if(_high == 0x40)
			return _port == 0 || sc8850 || m_model == DeviceModel::Xpgs || getDeviceProfile(m_model).groupCount <= 1;
		return _high == 0x50 && _port <= 1 && sc8850;
	}

	bool PartStateTracker::writeGlobal(const size_t _param, uint16_t _value, const bool _reply)
	{
		const auto& params = globalParams();
		const auto& param = params[_param];
		if(!isAvailable(param, m_model))
			return false;
		if(!_reply)
		{
			const auto efx = efxParamIndex(param.address);
			if(efx >= 0 ? !acceptsEfxValue(static_cast<size_t>(efx), _value) : !acceptValue(param, m_model, _value))
				return false;
		}

		const auto before = m_setup.globals;
		m_setup.globals[_param] = _value;

		// A macro sets the rest of its effect. A reply is what the board has: the rest comes in replies of its own.
		if(!_reply)
		{
			for(const auto& [address, value] : macroPreset(param.address, static_cast<uint8_t>(_value)))
			{
				const auto i = findGlobalParam(address);
				if(i >= 0 && isAvailable(params[static_cast<size_t>(i)], m_model))
					m_setup.globals[static_cast<size_t>(i)] = value;
			}
		}
		return before != m_setup.globals;
	}

	bool PartStateTracker::selectEfxType(const uint16_t _id, const bool _reply)
	{
		const auto& idx = indices();
		auto& globals = m_setup.globals;
		const auto before = globals;
		if(_reply)
		{
			globals[static_cast<size_t>(idx.efxType)] = _id;
			return before != globals;
		}

		// A type the board does not know is Thru. Selecting one, the same again too, sets its parameters to what the
		// type has them at and the sends to reverb, chorus and delay to 40, 0, 0; the controls stay.
		const auto& type = findEfxType(_id);
		globals[static_cast<size_t>(idx.efxType)] = type.id;
		for(size_t i = 0; i < g_efxParamCount; ++i)
			globals[static_cast<size_t>(idx.efxParam1) + i] = type.defaults[i];
		for(size_t i = 0; i < std::size(g_efxSendDefaults); ++i)
			globals[static_cast<size_t>(idx.efxToReverb) + i] = g_efxSendDefaults[i];
		return before != globals;
	}

	bool PartStateTracker::acceptsEfxValue(const size_t _index, const uint16_t _value) const
	{
		// The SC-8850 takes any value. The SC-88Pro only one for a parameter the type has, and of those it checks the
		// ones within their range.
		if(generation(m_model) == Generation::Sc8850)
			return _value <= 127;
		const auto& type = findEfxType(m_setup.globals[static_cast<size_t>(indices().efxType)]);
		const auto* param = findEfxParam(type, _index);
		return param && (!param->checked || (_value >= param->min && _value <= param->max));
	}

	bool PartStateTracker::writePart(Part& _part, const size_t _param, uint16_t _value, const bool _reply)
	{
		const auto& param = partParams()[_param];
		if(!isAvailable(param, m_model) || (!_reply && !acceptValue(param, m_model, _value)))
			return false;
		_part.setValue(_param, _value);
		return true;
	}

	bool PartStateTracker::selectionPerChannel() const
	{
		return generation(m_model) == Generation::Sc8850;
	}

	void PartStateTracker::dataEntry(Part& _part, RpnSelection& _selection, const uint8_t _value, const bool _lsb)
	{
		const auto& idx = indices();
		if(_selection.nrpnSelected)
		{
			if(_lsb || !_part.values[static_cast<size_t>(idx.rxNrpn)])
				return;
			if(const auto param = findPartParamByNrpn(_selection.nrpnMsb, _selection.nrpnLsb); param >= 0)
				writePart(_part, static_cast<size_t>(param), _value, false);
			return;
		}

		if(_selection.rpnMsb != 0 || !_part.values[static_cast<size_t>(idx.rxRpn)])
			return;
		const auto param = findPartParamByRpn(_selection.rpnLsb);
		if(param < 0)
			return;
		const bool sc8850 = generation(m_model) == Generation::Sc8850;
		// Of the RPNs here only the fine tune has a fine step. The SC-8850 writes it with the channel's last data entry
		// MSB, whatever the fine tune was set to since.
		if(_lsb)
		{
			if(param != idx.fineTune)
				return;
			if(sc8850)
				writePart(_part, static_cast<size_t>(param), _selection.fineTuneEntryMsb, false);
			_part.fineTuneLsb = _value;
			return;
		}
		// The SC-8850 starts the fine step over with a new coarse one, the SC-88 family keeps it
		if(writePart(_part, static_cast<size_t>(param), _value, false) && param == idx.fineTune && sc8850)
			_part.fineTuneLsb = 0;
	}

	Part* PartStateTracker::partAt(const uint8_t _group, const uint8_t _block)
	{
		const auto index = static_cast<size_t>(_group) * g_partsPerGroup + blockPart(_block);
		return index < m_setup.parts.size() ? &m_setup.parts[index] : nullptr;
	}

	void PartStateTracker::resetLocked(const Reset _reset)
	{
		m_setup = createDefaultSetup(m_model);
		// The SC-8850 keeps the RPN and NRPN its channels selected through a reset, the others forget them
		if(_reset == Reset::PowerOn || !selectionPerChannel() || m_selections.size() != m_setup.parts.size())
			m_selections.assign(m_setup.parts.size(), RpnSelection());
		if(_reset == Reset::PowerOn)
			m_gsResetSeen = false;
		else if(_reset == Reset::GsReset)
			m_gsResetSeen = true;

		// Most boards come up with NRPN off, which a GS Reset turns on. GM System On turns bank select off on the
		// boards that have the switch, and NRPN on all but the SC-55, which keeps it on, and the SCC-1A (and the
		// CM-300, untested), which put it back as it came up until a GS Reset arrived.
		const bool bankOff = _reset == Reset::GmSystemOn && hasRxBankSelect(m_model);
		bool nrpnOff = _reset == Reset::PowerOn && nrpnOffAtPowerOn(m_model);
		if(_reset == Reset::GmSystemOn)
		{
			switch(m_model)
			{
			case DeviceModel::Sc55Mk1: nrpnOff = false; break;
			case DeviceModel::Scc1a:
			case DeviceModel::Cm300: nrpnOff = !m_gsResetSeen; break;
			default: nrpnOff = true; break;
			}
		}
		for(auto& p : m_setup.parts)
		{
			if(bankOff)
				p.rxBankSelect = 0;
			if(nrpnOff)
				p.values[static_cast<size_t>(indices().rxNrpn)] = 0;
		}
		changed();
	}

	void PartStateTracker::changed()
	{
		if(m_activity.enabled())
			updateListeners();
		m_revision.fetch_add(1, std::memory_order_acq_rel);
	}

	void PartStateTracker::setActivityEnabled(const bool _enabled)
	{
		std::lock_guard lock(m_mutex);
		if(_enabled)
			updateListeners();
		m_activity.setEnabled(_enabled);
	}

	void PartStateTracker::updateListeners()
	{
		// A part hears the channel it is set to on its own group's port
		PartActivity::Listeners listeners{};
		for(size_t i = 0; i < m_setup.parts.size() && i < PartActivity::g_maxPorts * g_partsPerGroup; ++i)
		{
			const auto channel = m_setup.parts[i].rxChannel;
			if(channel < 16)
				listeners[i / g_partsPerGroup * 16 + channel] |= uint64_t{1} << i;
		}
		m_activity.setListeners(listeners);
	}
}
