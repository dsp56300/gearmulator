#include "midiRoutingMatrix.h"

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

	MidiRoutingMatrix::MidiRoutingMatrix()
	{
		for (auto& e : m_matrix)
			e.fill(EventType::None);

		// DEFAULT SETUP

		// that is the most straightforward and mandatory
		setEnabled(MidiEventSource::Device, MidiEventSource::Editor, EventType::All, true);
		setEnabled(MidiEventSource::Editor, MidiEventSource::Device, EventType::All, true);

		// editor will send messages to itself
		setEnabled(MidiEventSource::Editor, MidiEventSource::Editor, EventType::All, true);

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
}
