# Copilot TODO List

## MIDI Learn
- [ ] Add support for arbitrary MIDI channels
      - Currently, only parameters on channel 0 are supported. The mapping should be identical for all channels (ignore channel when matching), but when a message is received on a specific channel, it should update the parameter for that channel accordingly.
- [x] Add support for a second relative mode for values 63/65
      - Implemented: "Relative Signed" (0x01/0x7F) and "Relative Offset" (0x3F/0x41). Auto-detection distinguishes the two modes during learning. Backward compatible with old presets.
- [ ] Support Pitchbend
      - Only Control Change and Poly Pressure are currently supported for mapping. Allow users to map Pitchbend messages to parameters, including learning, mapping, and processing.
- [ ] Support Aftertouch
      - Channel Pressure (Aftertouch) is not currently supported for mapping or learning. Add support for mapping Channel Pressure (Aftertouch) messages to parameters.
- [ ] Support Poly Pressure
      - Poly Pressure is defined in the mapping type but not fully supported in learning or mapping logic. Implement full support for learning and mapping Poly Pressure (per-note aftertouch) messages.
- [ ] Add a "Browse" button to the UI that opens the config directory on disk
      - No UI element exists for browsing the config directory. Add a button to the MIDI Learn settings page that, when clicked, opens the directory containing MIDI Learn configuration files in the system file explorer.

