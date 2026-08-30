# Virus import fixtures

These fixtures are user-supplied captures containing disposable test sounds.

- `3Slimey-processor-state.hex`: the TI Control save-state MIDI blob captured on August 30, 2026, preserved unchanged as hexadecimal text. It contains one Multi dump, 16 Single dumps, two parameter SysEx messages, and 224 MIDI controller messages. Its first part is `3Slimey`.
- `3Slimey_part0.syx.hex`: the first part's complete 524-byte SysEx dump extracted unchanged from that save-state blob. Part number: 0; patch name: `3Slimey`.
- `Bank.mid`: a partial Bank A in Standard MIDI format, containing 10 complete Single dumps at zero-based programs 0–6, 21, 56, and 127. The test checks that missing slots are not padded or renumbered and names remain associated with their original programs.
- `BankFull.mid`: a full Bank A in Standard MIDI format, containing 128 complete Single dumps in sequential program order (0–127). First and last patch: `3Slimey`. The test checks every program number and selected names at 16-program boundaries.
- `single.syx`: a current-Single SysEx export captured directly from OsTIrus.
- `single.mid`: the same current Single exported as a Standard MIDI file.
- `arrangement.syx`: an Arrangement export captured in Multi mode, containing one Multi and all 16 part Singles.
- `arrangement.mid`: the same Arrangement exported as a Standard MIDI file.

The `.syx` and `.mid` exports remain in their native binary formats so tests exercise the actual file containers. Synthetic byte construction is reserved for malformed or incomplete input tests.
