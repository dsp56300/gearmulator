#include "midiRoutingMatrix.h"

#include "baseLib/configFile.h"

namespace synthLib
{
	constexpr std::string_view g_sourceNames[static_cast<uint32_t>(MidiEventSource::Count)] = 
	{
		"Unknown",
		"Device",
		"Editor",
		"Host",
		"Physical",
		"Internal"
	};

	static_assert(std::size(g_sourceNames) == static_cast<uint32_t>(MidiEventSource::Count));

	namespace
	{
		std::string createConfigKey(const MidiEventSource _source, const MidiEventSource _dest, const MidiRoutingMatrix::EventType _type)
		{
			return std::string("route_") + 
				std::string(MidiRoutingMatrix::toString(_source)) + '_' + 
				std::string(MidiRoutingMatrix::toString(_dest)) + '_' + 
				std::string(MidiRoutingMatrix::toString(_type));
		}
	}

	MidiRoutingMatrix::MidiRoutingMatrix()
	{
		for (auto& e : m_matrix)
			e.fill(EventType::None);

		// DEFAULT SETUP

		// that is the most straightforward and mandatory
		setEnabled(MidiEventSource::Device, MidiEventSource::Editor, EventType::All, true);
		setEnabled(MidiEventSource::Editor, MidiEventSource::Device, EventType::All, true);

		// device sends internal messages for LCD updates and LFO phases, only the editor needs it
		setEnabled(MidiEventSource::Internal, MidiEventSource::Editor, EventType::All, true);

		// allows full control of the device via physical ports
		setEnabled(MidiEventSource::Physical, MidiEventSource::Device, EventType::All, true);
		setEnabled(MidiEventSource::Device, MidiEventSource::Physical, EventType::All, true);

		// automation and plugin midi should reach the device and the editor
		setEnabled(MidiEventSource::Host, MidiEventSource::Device, EventType::All, true);
		setEnabled(MidiEventSource::Host, MidiEventSource::Editor, EventType::All, true);

		// the editor informs the host about parameter changes
		setEnabled(MidiEventSource::Editor, MidiEventSource::Host, EventType::All, true);

		// changes sent by the physical midi input should update the UI too
		setEnabled(MidiEventSource::Physical, MidiEventSource::Editor, EventType::All, true);
	}

	std::string_view MidiRoutingMatrix::toString(MidiEventSource _source)
	{
		return g_sourceNames[static_cast<uint32_t>(_source)];
	}

	std::string_view MidiRoutingMatrix::toString(const EventType _type)
	{
		switch (_type)
		{
			case EventType::None:          return "None";
			case EventType::Note:          return "Note";
			case EventType::SysEx:         return "SysEx";
			case EventType::Controller:    return "Controller";
			case EventType::PolyPressure:  return "PolyPressure";
			case EventType::Aftertouch:    return "Aftertouch";
			case EventType::PitchBend:     return "PitchBend";
			case EventType::ProgramChange: return "ProgramChange";
			case EventType::Other:         return "Other";
			case EventType::All:           return "All";
		}
		return {};
	}

	void MidiRoutingMatrix::saveChunkData(baseLib::BinaryStream& _binaryStream) const
	{
		baseLib::ChunkWriter cw(_binaryStream, "MiRM", 1);

		for (const auto& e : m_matrix)
		{
			for (const auto& f : e)
				_binaryStream.write(static_cast<uint8_t>(f));
		}
	}

	void MidiRoutingMatrix::loadChunkData(baseLib::ChunkReader& _cr)
	{
		_cr.add("MiRM", 1, [&](baseLib::BinaryStream& _data, uint32_t)
		{
			for (auto& e : m_matrix)
			{
				for (auto& f : e)
					f = static_cast<EventType>(_data.read<uint8_t>());
			}
		});
	}

	bool MidiRoutingMatrix::writeToFile(const std::string& _filename, const std::set<MidiEventSource>& _skipSources) const
	{
		baseLib::ConfigFile configFile;
		writeToFile(configFile, _skipSources);
		return configFile.writeToFile(_filename);
	}

	void MidiRoutingMatrix::writeToFile(baseLib::ConfigFile& _configFile, const std::set<MidiEventSource>& _skipSources) const
	{
		_configFile.add("version", "1");

		for (uint32_t i=0; i<m_matrix.size(); ++i)
		{
			const auto source = static_cast<MidiEventSource>(i);

			if (_skipSources.find(source) != _skipSources.end())
				continue;

			for (uint32_t j=0; j<m_matrix[i].size(); ++j)
			{
				const auto dest = static_cast<MidiEventSource>(j);

				if (dest == source)
					continue;

				if (_skipSources.find(dest) != _skipSources.end())
					continue;

				for (uint32_t e=1; e<=static_cast<uint32_t>(EventType::Last); e <<= 1)
				{
					auto eventType = static_cast<EventType>(e);

					const auto key = createConfigKey(source, dest, eventType);

					_configFile.add(key, enabled(source, dest, eventType) ? "1" : "0");
				}
			}
		}
	}

	bool MidiRoutingMatrix::readFromFile(const std::string& _filename)
	{
		baseLib::ConfigFile configFile(_filename);
		return readFromFile(configFile);
	}

	bool MidiRoutingMatrix::readFromFile(const baseLib::ConfigFile& _configFile)
	{
		if (_configFile.empty())
			return false;

		bool res = false;

		for (uint32_t i=0; i<m_matrix.size(); ++i)
		{
			const auto source = static_cast<MidiEventSource>(i);

			for (uint32_t j=0; j<m_matrix[i].size(); ++j)
			{
				const auto dest = static_cast<MidiEventSource>(j);

				for (uint32_t e=static_cast<uint32_t>(EventType::First); e<=static_cast<uint32_t>(EventType::Last); e <<= 1)
				{
					auto eventType = static_cast<EventType>(e);

					const auto key = createConfigKey(source, dest, eventType);

					if (!_configFile.contains(key))
						continue;

					const auto value = _configFile.getInt(key, -1);

					if (value == -1)
						continue;

					setEnabled(source, dest, eventType, value != 0);
				}

				res |= true;
			}
		}

		return res;
	}
}
