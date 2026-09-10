# Framework review of the 88EmuPlayer commit

Review of `d38b09835` ("88EmuPlayer: Sound Canvas emulation and its standalone player"),
restricted to `source/framework`, `source/cmake` and `source/3rdparty/libresample`.
That commit added a new emulator and, in the same breath, changed shared framework code
used by the six plugins it did not touch — Osirus, OsTIrus, Vavra, Xenia, NodalRed2x and
JE8086. Most of what follows lands on those six rather than on the Sound Canvas.

Work the list top down. Each entry is fixed on its own, reviewed, then committed.

Legend: `[ ]` open, `[x]` done, `[-]` deliberately not doing.

---

## A. Real-time and threading — fix before the release build

- [x] **A1 `jucePluginLib/processor.cpp:856` — `updateLatencySamples()` runs inside `processBlock`.**
      `setLatencySamples()` fires `updateHostDisplay(withLatencyChanged)`, which the VST3
      wrapper turns into a synchronous `IComponentHandler::restartComponent(kLatencyChanged)`
      from inside `process()` — forbidden by VST3, a known re-entrancy hang in Cubase and Live.
      It also takes `Plugin::m_lock` every block, the mutex the UI thread holds for the whole
      of `setDevice()`/`setState()`. Not hypothetical: `ResamplerInOut` bumps its latency on the
      audio thread while prewarming, which happens whenever host rate != device rate — the
      normal case for Xenia (40 kHz), Vavra (44.1 kHz), NodalRed2x and JE8086.
      Fix: only recompute when something actually changed, and never call the host from the
      audio thread — flag it and let the message thread publish it.

- [x] **A2 `synthLib/plugin.cpp:109` — device-rate switch rebuilds the resampler on the audio thread.**
      On a cache miss `setDeviceSamplerate()` reaches `recreate()`: two `new Resampler`, each
      `resample_open()` building a windowed-sinc table and allocating, plus a 512-sample prewarm.
      `Device::getDynamicSamplerates()` is a no-op in the base and nothing overrides it, so the
      cache is always empty and every runtime clock change takes that path. Multi-millisecond
      allocating stall inside `processBlock`.

- [x] **A3 `juceRmlUi/rmlRendererProxy.cpp:402` — `m_mutexRender` held across the entire frame.**
      Previously the queue was swapped into a local and executed unlocked. Now the message
      thread blocks in `finishFrame()` and `setRenderer()` for a whole frame, so every mouse and
      key event queues behind it. A render lambda that re-enters the proxy self-deadlocks on a
      non-recursive mutex it already owns; one that appends to `m_renderFunctions` invalidates
      the range-`for`. If a `func()` throws, the `clear()` is skipped and the batch replays,
      double-releasing handles.

- [-] **A4 `synthLib/plugin.cpp:123` — transport re-stamp promotes exactly the events it should drop.** RETRACTED, see below.
      On a discontinuity every event already staged in `m_midiIn` is re-stamped with the new
      generation. `processMidiInEvents()` has not run yet, so those are leftovers from earlier
      host blocks — the pre-discontinuity events the counter exists to discard. A Note On staged
      before the DAW stops survives the purge and arrives after the All Sound Off meant to cancel
      it. Hung note on transport stop.

      Not a defect. `Plugin::m_midiIn` does not survive a block: `processMidiInEvents()`
      drains the whole ring buffer and `m_midiIn.clear()` runs at the end of every
      `process()`, skipped only on the invalid-device early return. What it holds at the
      discontinuity check is what the overflow path staged since the last block, i.e.
      current-block events, which do belong to the new generation. The buffer that is
      carried across blocks is `ResamplerInOut::m_midiIn`, a different member - that one is
      C1.

- [x] **A5 `juceRmlUi/rmlMenu.cpp:140` — use-after-free moved, not removed.**
      The deleted unconditional `closeAll()` became a guard whose operands live in the click
      lambda itself. An action that swaps skin or renderer destroys the document, the menu root,
      the `div` and — via `EventListener::OnDetach`'s `delete this` — the `std::function` holding
      `weakSelf`/`observable`, while `ProcessEvent` is still inside it. The guard then reads freed
      heap.

- [-] **A6 `juceRmlUi/juceRmlComponent.cpp:480` — render-done guard installed after the early return.** RETRACTED, see below.
      The `hasRenderFunctions()` return sits above the `frameDone` ScopeGuard, so the one exit the
      comment warns about never releases the handshake. `m_renderDone` stays false, `timerCallback()`
      and `update()` both bail on it, and the editor stops repainting for the session.
      Same shape on the Metal viewport-zero path (`MetalContext.mm:165`) — see C4.

      Not a defect, and moving the guard would be a regression. `MetalContext::renderLoop`
      waits with a 16 ms timeout, so `renderMetal` runs at 60 Hz whether or not a repaint was
      requested. `hasRenderFunctions()` is false only when no frame was handed over - a
      spurious tick, or the one after the queue was drained - because `finishFrame()` always
      pushes an entry, empty or not. Releasing the handshake there would free a frame that
      was never consumed, and since `update()` clears `m_updating` before the render
      completes, that lets a second `update()` in. The real defect on this path is C4: the
      loop clears `m_repaintRequested` before the viewport gate, so a zero viewport eats the
      request and nothing re-arms `m_renderDone`.

## B. Rendering correctness

- [x] **B1 `juceRmlUi/rmlRendererProxy.cpp:282` — `SaveLayerAsTexture` is never restored.**
      It is the only handle-producing method using the non-registering `addRenderFunction(Func)`
      overload, and it backs `CallbackTexture`-cached box-shadow textures that persist across
      frames. After any renderer switch the handle resolves to `InvalidHandle` and the element
      silently draws nothing, permanently — RmlUi still thinks its handle is valid.
      Careful: switching to the two-arg overload is *not* the fix; a replayed layer snapshot
      captures whatever layer is current at restore time. The resource has to be invalidated and
      regenerated through RmlUi.

- [x] **B2 `juceRmlUi/rmlRendererProxy.cpp:270` — `PopLayer` can pop an empty stack.**
      `openGLContextClosing()` is the one renderer-switch site that does not take `ScopedAccess`
      and it runs on the GL thread. Under GL2 (`canLayer=false`) a `PushLayer` pushes nothing;
      the flip to software (`canLayer=true`) lets the matching `PopLayer` through, and `top()`
      hits an empty `std::stack`. Second path, no race needed: `setRenderer()` clears `m_handles`
      but never `m_layerHandles`, so the restore re-pushes a handle already on the stack.
      `m_config` is also read unlocked at nine sites and written under the mutex at one.

      The unlocked `m_config` read is NOT fixed and is now tracked as B6.

- [x] **B6 `juceRmlUi/rmlRendererProxy.cpp` — `m_config` is a data race.**
      Read without a lock at nine sites, written under `m_mutexRender` at one, from a
      different thread (`openGLContextClosing()` runs on the GL thread). B2 removed its
      worst consequence, the empty-stack pop, but the race itself stands. Either make the
      config atomic or take the lock on read.

- [x] **B3 `juceRmlUi/juceRmlComponent.cpp:488` — `getRenderSize()` hoisted above `ScopedAccess`.**
      The Metal render thread now walks the JUCE component tree — `getLocalBounds()`,
      `getParentComponent()`, `getTransform()` — unsynchronised against the message thread's
      `setBounds()`/`resized()`/`parentHierarchyChanged()`. A resize concurrent with a frame gives
      a torn `Rectangle<int>`.

- [x] **B4 `juceRmlUi/juceRmlComponent.cpp:805` — `focusLost()` key release skipped on Linux.**
      The new release-everything block was put inside the pre-existing
      `#if JUCE_WINDOWS || JUCE_MAC` guard, which is there for an unrelated mouse-leave quirk.
      Alt-tab away with a key held and it stays down in RmlUi and in `m_pressedKeys` forever;
      `keyPressed()` push_backs unconditionally including auto-repeat, so the vector grows on
      every focus loss and the stale modifier alters later knob drags.

- [-] **B5 `juceRmlUi/juceRmlComponent.cpp:686` — wheel rescale only compensated in `ElemKnob`.** RETRACTED, see below.
      A detent goes 0.234 -> 1.0 units on Windows (4.27x), 0.195 -> 1.0 on Linux, 0.039 -> 1.0 on
      macOS (25.6x); RmlUi then multiplies by `UNIT_SCROLL_LENGTH` = 80. `rmlElemKnob.cpp` was
      retuned `/7.5f` -> `/32.0f`, but `ElemList::onMouseScroll` re-dispatches the delta verbatim,
      as does every native `overflow:auto` scroller. The Patch Manager grid is in all six plugins.
      Note the old macOS behaviour (3 dp per detent) was itself broken — this wants a calibration
      pass on lists, not a revert.

## C. Device / MIDI behaviour

- [x] **C1 `synthLib/resamplerInOut.cpp:232` — equal-rate fast path skips `clampMidiEvents`.**
      It prepends MIDI staged during an earlier block without clamping to the current block
      length, unlike the resampling path. Offsets can exceed `_numSamples`; a device indexing a
      per-block array by offset reads out of bounds, and at minimum note order inverts.

- [x] **C2 `synthLib/midiRateLimiter.cpp:96` — a second discontinuity erases the All Sound Off.**
      `m_activeChannels` is cleared when the ASO is *queued*, not sent, and the purge loop drops
      still-queued ASOs from an older generation. Two discontinuities inside the drain window
      leave the notes hanging. Verified by compiling and running the file: wire output is empty
      where a single discontinuity emits `b0 78 00`. Latent today — nothing calls
      `transportDiscontinuity()` in production.

- [x] **C3 `hardwareLib/hd44780.cpp:174` — Clear/Home do not leave CGRAM mode.**
      Only "Set DDRAM address" clears `m_cgRamMode`, so *define glyphs -> clear -> write text*
      puts the text in CGRAM: glyph corrupted, panel blank. Same decoder: instruction `0x00` is
      unclaimed by every mask and falls into the trailing `else` as "Set DDRAM address", so
      firmware idling with `0x00` between Set CGRAM and its data silently resets the counter.

- [x] **C4 `juceRmlUi/MetalContext.mm:165` — zero viewport latches with no recovery.**
      `updateDrawableSize()` was removed from `renderLoop()` in favour of a
      `m_viewportWidth > 0` gate. `renderMetal` is the only thing that releases the frame, so a
      zero viewport at post time leaves `m_renderDone` false with nothing to clear it — and a
      plain host-driven resize does not re-arm it. Simplest fix: call the listener anyway and let
      `renderMetal`'s own size guard drop the frame through the ScopeGuard.

- [x] **C5 `hardwareLib/sed1335.cpp:79` — CSRR wedges the data path.** Premise was WRONG, see below.
      `writeCommand` arms CSRR with `startParam(0x47, 2)` but `writeData()` has no `0x47` branch,
      so the parameter bytes hit the terminal `else { return; }` before the `m_stage`/
      `m_dataRemaining` bookkeeping. `m_mode` stays `0x47` forever and every later `writeData()`
      is dropped — panel freezes on its last content.

- [x] **C6 `hardwareLib/sed1335.cpp:73` — `m_dirty` not set for SCROLL / DISP OFF / HDOT SCR.**
      Only MWRITE and DISP ON set it, so a driver that double-buffers by writing a page and
      flipping with SCROLL produces no notification and the UI keeps showing the old page.

- [x] **C7 `hardwareLib/hd44780.cpp:205` — one-line mode shift wraps at the wrong modulus.**
      `m_displayShiftOffset` is reduced `% Columns` (40) while one-line DDRAM is 80 cells and
      `advanceDdAddr` wraps at 0x4f, so cells 0x28-0x4f can never be scrolled into view.

- [x] **C8 `jucePluginEditorLib/lcd.cpp:249` — blink predicate disagrees with the draw predicate.**
      `wantBlink` checks only `>= 0`; the drawing code additionally requires `< m_numCharsX/Y`.
      A cursor parked outside the visible window arms the 500 ms blink timer forever, repainting
      the whole LCD at 2 Hz with nothing on screen changing.

## D. Cross-platform and tooling

- [x] **D1 `baseLib/filesystem.cpp:339` — `getFileModificationTime` unit differs by platform.** Mostly overstated, see below.
      `st_mtime` seconds on the dirent branch, raw `file_time_type` ticks (100 ns since 1601 on
      MSVC) otherwise, while the header documents "seconds since the epoch". The new 88emu
      romloader uses it as a cache key. Pre-epoch files wrap through `static_cast<uint64_t>`, and
      both branches return 0 on stat failure so an unreadable file caches as unchanged forever.

- [x] **D2 `tools/changelogGenerator/changelogGenerator.cpp:322` — off-by-one yields an empty product.**
      The multi-`/` split uses `begin <= product.size()`, so a heading ending in `/` produces a
      trailing empty name and an output file with an empty basename.

- [x] **D3 `synthLib/romLoader.cpp:21` — lazy static init is unsynchronised.**
      `static bool s_initialized` with no mutex, now also reached from the mutating
      `addSearchPath()`. `Processor::getPlugin()` documents concurrent first-callers, so two
      threads can insert into `g_searchPaths` at once.

- [x] **D4 `synthLib/romLoader.cpp:28` — the process working directory is now always searched.**
      Previously the defaults were skipped whenever a caller had registered a path, which every
      plugin does. Low severity: JE8086 validates ROM content, not just size, so this is an
      ordering quirk rather than a hijack — but a DAW's cwd is arbitrary.

- [x] **D5 `juceRmlUi/juceRmlComponent.cpp` ctor failure path — cursor callback not cleared.**
      The constructor installs a `this`-capturing callback and rethrows on failure without
      clearing it, so the lambda outlives an object whose destructor never runs.

## E. Efficiency

- [x] **E1 `synthLib/plugin.cpp:301` and `:219` — a whole `SMidiEvent` copied per event on the RT thread.**
      `auto event = _ev;` exists only to stamp a field that `stampTransportGeneration` skips for
      sysex, so the copy allocates precisely where it is useless, and the event is copied again
      into the vector. Push first, stamp in place.

- [x] **E2 `synthLib/resamplerInOut.cpp:249` — every incoming event copied twice per block.**
      Into `m_scaledMidiIn`, then into `m_midiIn`. The scratch member exists only because
      `scaleMidiEvents` clears its destination; an append loop removes both the copies and the
      member.

- [x] **E3 `synthLib/midiRateLimiter.cpp:65` — `deque::erase` inside the iteration is O(n^2).**
      One `remove_if` compaction plus a single `erase(it, end())` is O(n).

- [x] **E4 `hardwareLib/sed1335.cpp:299` — `renderMono` recomputes per pixel what is constant per cell.**
      Two divides and two modulos where one pair suffices, the character byte and glyph row
      re-fetched per pixel, and a loop-invariant `pitch` re-evaluated in the inner loop.

## F. Shared code bent for one caller

- [x] **F1 `synthLib/device.h:57` — `getDynamicSamplerates()` has no implementer.** Kept, see below.
      Nothing overrides it, yet it costs a per-block `getSamplerate()` poll on the audio thread
      (see A2) plus a duplicate `ResamplerInOut` per declared rate and a hand-written 12-field
      `swapStream()` that silently misses any member added later.

- [-] **F2 `synthLib/plugin.h:58` — `setMidiClockEnabled()` has two callers, both passing `false`.** RETRACTED, see below.
      Every other plugin carries a flag it never asked about, it must be re-applied on every
      device replacement, and the condition is now spelled in three places.

- [-] **F3 `jucePluginEditorLib/lcd.h:64` — `LcdConfig`, `setCursor`, blink timer, `onClicked` all unused.** Partly wrong, not deleting, see below.
      Fifteen `constexpr` constants became `const` instance members (which also makes `Lcd`
      non-assignable) to parameterise one float nobody passes; all three subclasses still use the
      three-argument constructor; the underline renderer is unreachable. The Sound Canvas panel
      does not use this class at all.

- [x] **F4 `synthLib/midiTypes.h:83` — `isUniversalTuningSysex()` has no callers.** Kept and tested, see below.
      Magic MIDI Tuning byte patterns in the header every synth includes, with no consumer and no
      test. Wire it up or delete it.

- [-] **F5 `juceRmlUi/juceRmlComponentConfig.h:22` — `includeDefaultTemplates` is an all-or-nothing opt-out.** RETRACTED, see below.
      The real defect is that three fixed templates are spliced into every document whether it
      references them or not. `additionalTemplateFiles` already models "inject because the
      document asked".

- [ ] **F6 `juceRmlUi/rmlElemComboBox.cpp:166` — a second dropdown implementation with hard-coded colours.**
      `popup="window"` bypasses the skinning system entirely — `0xff252b30`, `Font(13.0f)`,
      `withStandardItemHeight(22)` compiled into framework code. Motivation was that the
      in-document menu wraps into columns instead of scrolling; making `Menu` scroll fixes that
      for every plugin and leaves one menu code path instead of two.

- [ ] **F7 `juceRmlPlugin/rmlParameterBinding.h:40` — `evElementDestroyed` has no subscribers.**
      Declared, documented with a five-line contract, invoked — and the case its comment names is
      already covered by `evUnbind`, fired one line earlier. A teardown hook that does nothing.

- [ ] **F8 `juceRmlPlugin/rmlPluginContext.cpp:15` — per-frame rebinding sweep replaced an assert.**
      Binding failure used to `assert(false)`; now it is a supported steady state retried at frame
      rate for the life of the editor, constructing a `std::string` and doing a parameter lookup
      per pending element per frame. Bind on attach instead.

## G. Duplication

- [ ] **G1 `hardwareLib/hd44780.*` duplicates `hardwareLib/lcd.h`.**
      Same chip, same namespace, overlapping API down to identical signatures — and the new
      `sed1335.h` comment even calls itself "sibling of the HD44780 character LCD (lcd.h)".
      `Hd44780` is the better implementation (real 80-cell DDRAM, correct shift model);
      `hwLib::LCD` has a hard-coded 40-byte DDRAM and a manual memmove. Migrate Vavra
      (`mqLib/lcd.h`) and JE8086 (`jeLib/jeLcd.h`, plus `sysexRemoteControl`) onto `Hd44780` and
      delete `LCD`. Depends on C3 and C7 being fixed first.

- [ ] **G2 `baseLib/filesystem.cpp:225` — `findFilesRecursive` re-implements `findFiles`.**
      `findFiles` is `findFilesRecursive` at depth 0 — its own caller in `romLoader.cpp:57`
      is an if/else differing only in the depth argument. Forwarding also fixes `findFiles`
      returning directories whose name matches the extension.

- [ ] **G3 `baseLib/filesystem.cpp:194` — `statEntry` duplicates `isDirectory` and `getFileSize`.**
      Combining two stats into one is a fair reason for a helper, but the copy carries two fixes —
      `u8path` instead of a narrow string, and an `error_code` instead of reading an uninitialised
      `statbuf` — that were not applied to the public helpers. Back-port them.
      `getFileModificationTime` is a fourth copy of the same `#ifdef` pair (see D1).

- [ ] **G4 `synthLib/resamplerInOut.cpp:40` — offset rescale pasted twice, and `scaleMidiEvents` exists.**
      The same loop appears in `setDeviceSamplerate` and `setSamplerates`; the class already has
      the helper, unusable in place only because it clears its destination.

- [ ] **G5 `plugin.cpp:341` / `midiRateLimiter.cpp:52` — `isTransportBound` written twice.**
      The write side and the read side of one protocol. If they ever disagree an event either
      survives a seek forever or is never flushed. `midiTypes.h` is the natural home.

## H. Simplification

- [ ] **H1 `synthLib/resamplerInOut.cpp:47` — a swap that cancels the next line's swap.**
      `alternative->m_midiIn.swap(m_midiIn)` undoes what `swapStream()` does one line later, so the
      queue ends where it started via two operations. Anyone deleting either half silently hands
      the live MIDI queue to a cached resampler. Also: `prepareAlternatives()` is called at four
      sites, every one immediately after `recreate()`.

- [ ] **H2 `synthLib/midiRateLimiter.cpp:120` — the queue-pop block is pasted twice.**
      Sixteen lines duplicated, differing only in `break` vs `return`, wrapped in a `while(true)`
      whose inner loop already guarantees the outer condition. One `popNextEvent()` collapses
      roughly forty lines to five.

- [ ] **H3 `juceRmlUi/rmlElemComboBox.cpp:39` — three lifetime layers for a stateless object.**
      A `static weak_ptr` cache, a per-element `shared_ptr` member and a capture in the async
      callback, for an immutable four-colour `LookAndFeel`. A function-local `static` is one line.
      The header also documents an option that has no member, setter or code path.

## I. Conventions (CLAUDE.md)

- [ ] **I1 `synthLib/device_test.cpp` and `midiRateLimiter_test.cpp` are space-indented.**
      "Tabs for indentation (tab size 4, UseTab: Always)". `device_test.cpp` is 58 space lines and
      0 tab lines; `midiRateLimiter_test.cpp` mixes both. `dynamicSamplerate_test.cpp`, added in
      the same commit, is correctly tabbed.

- [ ] **I2 Test-class members lack the `m_` prefix.**
      "`m_` member prefix" — `device_test.cpp:20-21` (`midiSent`, `transportEvents`) and
      `dynamicSamplerate_test.cpp:28-29` (`rate`, `samples`).

- [ ] **I3 `hardwareLib/sed1335.cpp:131` exceeds the 120-column limit** (133 at tab=4).

---

## Checked and cleared

Recorded so they are not re-litigated:

- **Bridge wire format** — the new `SMidiEvent` fields are not stale on the receiver, they hold
  their in-class initialisers. Transport markers never reach a bridged device because
  `device.cpp:39-45` swallows them a layer above the bridge, which happens for local devices too.
- **`m_deviceSamplerate` without a `> 0` guard** — no `Device` subclass in the tree can return 0,
  and both divisions are already guarded. The change is a fix: the old code cached the *requested*
  rate even when `setSamplerate()` failed.
- **Shared cursor callback** — every `RmlComponent` owns its own `RmlInterfaces`, including the
  88emu settings window.
- **`callAsync` stranding `m_renderDone`** — `m_openGLContext` is assigned in only two places, so
  a null pointer means an earlier run of the same lambda already set the flag.
- **`MidiRateLimiter` is dead** — false. JE8086 holds one and routes every inbound event through
  it. Only `transportDiscontinuity()` has no production caller.
- **Unreleased-device naming on the public remote** — `doc/restructure_plan.md` §9 is scoped to
  *unreleased* devices, and this is the commit that releases the Sound Canvas.
- **Include paths, brace style, `_` parameter prefix, `getState()` append semantics** — clean.
- **F5, the default templates** — the premise and my suggested alternative were both wrong. All
  three templates are instantiated from C++ by name (`createFromTemplate("settings")`,
  `"colorpicker"`, `src="patchmanager"`), so a document cannot know it will be asked for them and
  cannot declare them on demand. 9 of 11 skins already declare colorpicker and patchmanager
  themselves; NOTHING declares tus_settings, so the injection is what gives every plugin a settings
  dialog. And `additionalTemplateFiles` is not document-driven either - pluginEditor scans for
  per-product `tus_settings_<product>.rml`. Documented what the flag really controls instead.
- **F4, the tuning sysex predicate** — no caller, but it is an `inline` free function so an
  uncalled one costs nothing. Both patterns check out against the spec (Bulk Dump `F0 7E dd 08 01`,
  Master Fine Tuning `F0 7F dd 04 03 ll mm F7` at exactly 8 bytes). The risk was that it is
  unverified byte matching in the header every synth includes, so it is now pinned by tests in
  `synthLibTests` instead of deleted - near misses in the same universal families, manufacturer
  dumps and malformed input included.
- **F3, the Lcd surface** — checked all four claims. `setCursor()` genuinely has no caller
  anywhere, so the blink timer and the underline renderer have never run (which means the C8 fix
  landed in unreached code - correct, but untested by anything). `LcdConfig` is always built with
  the default 3.0f because all three subclasses use the three-argument constructor. But `onClicked`
  is NOT unused - `lcd.cpp:48,57` call it for the override-text splash; that claim was wrong. And
  `_pixelSpacing` is a documented extension point, not an accident. Following the F1 decision,
  keeping rather than deleting: the note is now in the header so the cursor path is known-undriven.
- **F2, the MIDI clock flag** — not shared code bent for one caller; it is load bearing.
  `MidiClock::process()` advances its tick counter from `_bpm` alone and never consults
  `_isPlaying`, so the standalone player - which passes a fixed 120 bpm and `isPlaying=false` on
  every block - would emit `M_TIMINGCLOCK` into the Sound Canvas forever without it. Deleting the
  flag would break the player. The three sites that read it are three different actions (restart on
  enable, restart after a state load, run the clock), not a duplicated condition. Documented why it
  exists so nobody removes it as unused; the residual hazard - a future third construction site
  forgetting it - is now named in the header.
- **F1, the dynamic-samplerate subsystem** — no implementer today (verified: no device's rate moves
  at runtime, 88emu model switching replaces the whole `Plugin`, `setState` re-reads the rate), but
  it is NOT speculative. The TC M-One XL selects its sample rate from a front panel menu, i.e. the
  firmware changes the clock while running, and the per-block `getSamplerate()` poll is the only
  thing that would notice. Kept and documented rather than deleted, on the user's call.
  A device opts in by overriding `getDynamicSamplerates()`; that is the whole contract.
- **D1's consequences** — only the header comment was wrong. Both romloader caches are
  process-local `static std::map`s, so the platform unit never crosses a boundary and every use
  is an equality test. The 0-on-failure sentinel cannot equal a real stamp, so it forces a cache
  *miss* and a re-read - the safe direction, not "unchanged forever" as filed. Pre-epoch wrap is
  harmless for equality. Fixed the documented contract; deliberately did not `duration_cast` the
  Windows branch down to seconds, which would lose change detection within a second.
- **C5's stated mechanism, the write-path wedge** — wrong on both counts. CSRR takes no written
  parameters at all (it is a read command, answered by two `readData()` calls), and
  `writeCommand()` reassigns `m_mode` unconditionally, so nothing can stay armed past the next
  command byte. The real defect underneath was on the read side: `readData()` answered CSRR with
  VRAM at the cursor instead of the cursor address, so a driver asking where the cursor ended up
  got a pixel byte and wrote to a garbage address. Fixed by implementing CSRR properly.
- **B5, wheel rescale** — traced every consumer of the delta. `ElemComboBox`, the radio-button
  handler in `rmlPluginDocument` and `rmlControllerLink` read the sign only; `ElemList::onMouseScroll`
  swaps the axes and leaves the magnitude alone; the slider handler delegates to `ElemKnob`. The
  only magnitude consumers are `ElemKnob`, which was retuned, and RmlUi's native scroller, which
  multiplies by `UNIT_SCROLL_LENGTH` (`Context.cpp:826`) — 80dp per notch is exactly the convention
  it is written for, the same one its SDL/GLFW backends feed. The old raw delta gave that scroller
  19dp per detent on Windows and 3dp on macOS. The rescale is the fix, not the regression, and it
  makes the knob uniform at 3.1% of range per detent instead of 3.1% on Windows / 0.5% on macOS.

## Pre-existing, not from this commit

Worth fixing, but do not attribute them to the 88emu work:

- `plugin.cpp:361` — partial-sysex handling falls through and delivers the raw fragment as well as
  the reassembled message (2021, `e8d79d02c`). Re-confirmed against `d38b098353^` while doing E1:
  the `if (!_ev.sysex.empty())` block needs a `return` at its end. A middle or end chunk is
  appended to `m_pendingSysexInput` and then *also* pushed raw, so the device sees a headless
  fragment alongside the reassembled message. Reachable whenever hardware splits a dump.
- `processor.cpp:812` — the doubled-`F7` branch erases the **front** byte (the `F0`) instead of the
  duplicate tail; copy-paste of the doubled-`F0` branch above it. Host-reachable via VST3
  double-wrapping, and it produces exactly the state that walks into the fall-through above.
- `rmlMenu.cpp:47` — `if (!isOpen()) close();` is inverted. Latent: every current caller allocates
  a fresh `Menu`, so the second-open path is unreachable.
- `midiRateLimiter.cpp:150` — channel voice messages jump ahead of queued sysex because everything
  non-sysex goes into `m_pendingRealtime`. Live on JE8086; only System Real Time is entitled to
  overtake.
