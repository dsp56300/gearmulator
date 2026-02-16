# Copilot TODO List

## MIDI Learn
- [ ] Add support for arbitrary MIDI channels
      - Currently, only parameters on channel 0 are supported. The mapping should be identical for all channels (ignore channel when matching), but when a message is received on a specific channel, it should update the parameter for that channel accordingly.
- [x] Add support for a second relative mode for values 63/65
      - Implemented: "Relative Signed" (0x01/0x7F) and "Relative Offset" (0x3F/0x41). Auto-detection distinguishes the two modes during learning. Backward compatible with old presets.
- [x] Support Pitchbend
      - Implemented: PitchBend learning, mapping, and value processing. Uses 14-bit value (LSB/MSB) for absolute mode. Always learned as Absolute mode. UI shows "-" for controller column.
- [x] Support Aftertouch
      - Implemented: Channel Pressure (Aftertouch) learning, mapping, and value processing. Value extracted from byte b. Always learned as Absolute mode. UI shows "-" for controller column.
- [x] Support Poly Pressure
      - Implemented: Full Poly Pressure learning and mapping. Uses note number as controller, pressure value from byte c. Always learned as Absolute mode.
- [ ] Add a "Browse" button to the UI that opens the config directory on disk
      - No UI element exists for browsing the config directory. Add a button to the MIDI Learn settings page that, when clicked, opens the directory containing MIDI Learn configuration files in the system file explorer.

