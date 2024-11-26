#include "midiTranslator.h"

#include "midiTypes.h"

#include <cstddef>

namespace synthLib
{
	MidiTranslator::MidiTranslator()
	{
		reset();
	}

	void MidiTranslator::process(std::vector<SMidiEvent>& _results, const SMidiEvent& _source)
	{
		const size_t size = _source.sysex.size();

		if (!size)
		{
			if (_source.a < 0xf0)
			{
				auto& targets = m_targetChannels[_source.a & 0x0f];

				for (auto target : targets)
				{
					auto& result = _results.emplace_back(_source);
					result.a = static_cast<uint8_t>((_source.a & 0xf0) | target);
				}
			}
			else
			{
				_results.push_back(_source);
			}

			return;
		}

		if (size < 4 || _source.sysex.front() != 0xf0 || _source.sysex.back() != 0xf7 || _source.sysex[1] != ManufacturerId)
		{
			_results.push_back(_source);
			return;
		}

		switch (_source.sysex[2])
		{
		case CmdSkipTranslation:
			if (size == 7)
			{
				auto& result = _results.emplace_back(_source);
				result.a = _source.sysex[3];
				result.b = _source.sysex[4];
				result.c = _source.sysex[5];
				result.sysex.clear();
			}
			break;
		case CmdAddTargetChannel:
			if (size == 6)
			{
				const auto sourceChannel = _source.sysex[3];
				const auto targetChannel = _source.sysex[4];
				addTargetChannel(sourceChannel, targetChannel);
			}
			break;
		case CmdResetTargetChannels:
			reset();
			break;
		case CmdClearTargetChannels:
			clear();
			break;
		default:;
		}
	}

	bool MidiTranslator::addTargetChannel(const uint8_t _sourceChannel, const uint8_t _targetChannel)
	{
		if (_sourceChannel >= 16 || _targetChannel >= 16)
			return false;
		m_targetChannels[_sourceChannel].insert(_targetChannel);
		return true;
	}

	void MidiTranslator::reset()
	{
		for (uint8_t i = 0; i < static_cast<uint8_t>(m_targetChannels.size()); ++i)
			m_targetChannels[i].insert(i);
	}

	void MidiTranslator::clear()
	{
		for (uint8_t i = 0; i < static_cast<uint8_t>(m_targetChannels.size()); ++i)
			m_targetChannels[i].clear();
	}

	SMidiEvent& MidiTranslator::createPacketSkipTranslation(SMidiEvent& _ev)
	{
		if (_ev.sysex.empty())
			_ev.sysex = { 0xf0, ManufacturerId, CmdSkipTranslation, _ev.a, _ev.b, _ev.c, 0xf7 };
		return _ev;
	}

	SMidiEvent MidiTranslator::createPacketSetTargetChannel(const uint8_t _sourceChannel, const uint8_t _targetChannel)
	{
		SMidiEvent e;
		e.sysex = { 0xf0, ManufacturerId, CmdAddTargetChannel, _sourceChannel, _targetChannel, 0xf7 };
		return e;
	}
}
