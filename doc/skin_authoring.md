# Authoring a skin

How to get from "this device needs a UI" to a skin that looks designed rather
than generated, and the RmlUi behaviour that will bite on the way. Written from
building one plugin's edit and multi pages end to end; everything below was
measured in a running plugin, not inferred from the docs.

Read [`mcp_server.md`](mcp_server.md) first if you have not driven a plugin from
a script before — the loop here depends on it.

## 1. Settle the direction before writing any RML

The failure mode is not ugly CSS, it is going straight from a parameter list to
a layout. That produces a page that is *correct* and says nothing about the
instrument.

Draw two to four genuinely different directions first — as a design canvas, a
sketch, anything — and have someone pick one. Cheap rules that paid off:

- **Show the real parameter set in every option.** Density is most of the design
  decision, and an option drawn with placeholder controls hides it.
- **Give each option its tradeoff, including the one you prefer.** A set where
  only your favourite has a case made for it is a rigged vote.
- **Let the machine suggest the layout.** The winning direction grouped the
  panels the way the synth's own signal path pairs them, so the layout teaches
  the instrument instead of just listing it. That is worth more than any
  amount of styling.

Expect the mockup to be wrong in detail. A drawn envelope curve became six stage
faders, because RmlUi cannot draw a curve without a Lua canvas and stages you can
grab beat a picture you cannot. Note those deviations somewhere — they are design
decisions, not compromises.

## 2. The iteration loop

Skins can be loaded from disk, so **you do not have to rebuild to see a change**:

- On-disk skins live in `<dataFolder>/skins/<name>/`
  ([`pluginEditorState.cpp:136`](../source/framework/juce/jucePluginEditorLib/pluginEditorState.cpp#L136)).
  Point the plugin's config `skinFolder` at one and it loads from there.
- Symlink or junction that folder to the skin folder in the repo, so the repo
  stays the source of truth and you are never editing a copy you will lose.
- `F5` reloads the skin in place ([`pluginEditorState.cpp:229`](../source/framework/juce/jucePluginEditorLib/pluginEditorState.cpp#L229)),
  gated on the `reloadSkinViaF5` config flag which the Skin settings page toggles.
  The MCP `send_key` tool maps the string `"f5"`
  ([`mcpDomTools.cpp:251`](../source/framework/juce/jucePluginEditorLib/mcpDomTools.cpp#L251)).

So the loop is: edit the `.rml`/`.rcss` in the repo, press F5, screenshot. No
rebuild, no relaunch.

**If the editor is gone after an F5, the reloaded skin did not load.** A stylesheet
or document error leaves no document behind, and the MCP tools then report
"Plugin editor is not open" — which reads like the reload closed the window, and
is not. There is usually a modal sitting on top saying which property or file
failed. Check for that before blaming the reload; a relaunch will appear to
"fix" it only because you rebuilt or reverted the broken file on the way.

**Parameter descriptions are not on that path.** The `parameterDescriptions_*.json`
is compiled in as binary data, so any change to it needs a rebuild. Same for new
skin *assets*: the file list is a CMake `GLOB`, so a new `.png` needs a
reconfigure before it is bundled.

**Measure, do not eyeball.** ImageMagick turns "is it right yet" into a number
(`magick compare -metric RMSE`), and cropping a region at 400% is how you settle
questions like "is the fill actually aligned with its track" in one look instead
of three rounds of guessing.

## 3. Generate repetitive markup

Four identical source panels, or eight identical multi rows, differ only by an
index. Write a small script that emits them and splices the page into the `.rml`
by element id. Hand-copying eleven cells eight times is how one column quietly
ends up pointing at the wrong section, and a generated page cannot drift.

Keep the generator out of the skin folder if it lives in the repo — the skin
`GLOB` in [`skins.cmake`](../source/cmake/skins.cmake) takes
`*.png *.rml *.rcss *.ttf *.lua *.svg`, so a `.py` beside the artwork ships with
the source without being bundled into the binary.

## 4. Artwork without an artist

You are not limited to flat rectangles, and you do not need a bitmap editor.

- **SVG is enabled.** `RMLUI_SVG_PLUGIN` is forced on in
  [`source/3rdparty/CMakeLists.txt:39`](../source/3rdparty/CMakeLists.txt#L39),
  so a skin can use `<svg src="thing.svg"/>` directly.
- **Knobs want a spritesheet**: `frames: N; spriteprefix: pfx_; decorator: image(pfx000 contain);`
  plus an `@spritesheet` block naming every frame's rect. Generate it: write
  **one** SVG containing all N frames laid out in a grid and rasterise it in a
  single pass, then emit the matching `@spritesheet` text from the same script.
  N separate exports take minutes and add a montage step to get wrong; one takes
  seconds. Re-tinting the whole set is then a one-line change.
- **Fonts are open.** Skins bundle their own `.ttf` — several shipped skins
  already do. You are not stuck with the two faces the framework bundles.
- **But name only a face that exists.** RmlUi does not fall back to a system
  font: every element inheriting an unmatched family renders *no text at all*,
  and the load errors raise a modal that blocks input.

## 5. RCSS behaviour that will cost you an afternoon

Each of these was found the slow way.

**Sizing is content-box.** Padding and borders are added outside a stated width.
Three columns measured to a grid came to 1198dp instead of 1150 and the last one
was pushed off the edge. Put `box-sizing: border-box` on everything that is
measured to a grid.

**`sliderprogress` is a child of `slidertrack`**
([`WidgetSlider.cpp:111`](../source/3rdparty/RmlUi/Source/Core/Elements/WidgetSlider.cpp#L111)),
not a sibling. So a `margin-top` used to centre the track is applied a *second*
time to the fill inside it, and the fill sits low by exactly that much. Give the
fill `margin-top: 0` and its track's height. A percentage height overflows —
state the number.

**A vertical range fills from the thumb upwards.** For an envelope stage that
draws it upside down: value 0 painted a full bar and value 100 an empty one. Do
not invert the value — put the tint on the `slidertrack` and paint the
`sliderprogress` in the background colour, so the visible fill is the part
*below* the thumb. The thumb was always in the right place.

**Text directly inside a flex container is dropped.** A header cell holding a
bare column name cannot share a class that sets `display: flex` with data cells
that hold child elements. The row renders empty and nothing warns you.

**A `div` inside a `button` needs `display: block`.** Otherwise it is laid out
inline, takes no width, and is invisible while sitting correctly in the DOM —
`find_elements` reports `w: 0` with the element present, which is the tell.

**A combo will not go below about 92dp**, and it does not stretch to its flex
column — it settles at its own minimum and clips its text. State its width.
When you budget a row of them, count the per-cell margins too; forgetting eleven
2dp margins is the difference between fitting and not.

**RCSS has no `inherit` keyword.** `line-height: inherit` is a parse error, and a
parse error raises a modal at load. Combo text is a child element, so the
line-height has to reach it explicitly or the text pins to the top left.

**Equal-specificity ties go to whichever rule came FIRST**, the opposite of CSS.
An override needs higher specificity — two classes (`.knob.knobbig`), not a
later single-class rule.

**Gradients and flat overlays do not compose over tall elements.** A gradient
across a 700dp panel runs from lighter than a row stripe at the top to darker at
the bottom, so the first stripes vanish and the last read inverted. Either keep
the gradient shallow and the stripe clear of both ends, or do not gradient the
element you are striping.

Also confirmed unsupported: `text-overflow: ellipsis` (raises the same modal).
Fine: `white-space: nowrap`, `overflow: hidden`, `pointer-events: none`,
`opacity`, `border-radius`, `decorator: linear-gradient(180deg, a, b)`.

## 6. The parameter model

**A part is one multitimbral slot.** It is not a slot for anything else. If a
patch has several oscillators, sources or layers, those are *not* parts — the
whole patch is one part, and each element gets its own parameter names
(`Osc1Shape`, `Osc2Shape`), which is what every device here already does. Using
the part axis for both gives you `part 2` meaning two different things on two
pages, and it will not survive contact with a multi mode.

**Parameters are identified by `{page, part, index}`**
([`controller.cpp:68`](../source/framework/juce/jucePluginLib/controller.cpp#L68)).
Names must be unique, but the `index` may repeat — which matters when several
copies of one parameter share a sysex number. **On a collision nothing
complains**: the later ones are registered as *derived* parameters and their
values are linked ([`controller.cpp:89`](../source/framework/juce/jucePluginLib/controller.cpp#L89)).
The symptom is four controls that move as one. Give them distinct **pages** —
the page is part of the identity, and it doubles as a clean discriminator to
read the element index back off.

**Value lists are cheap and worth it.** A parameter that is really a note number,
a waveform or a mode should be a combo over a named list, not a slider you drag
to read. Note-name lists already exist in several devices' descriptions — copy
one rather than typing 128 strings, and note that the repo carries **both**
octave conventions (`C-2` at 0 and `C-1` at 0), so check which the device prints.

**A midi packet needs every field it declares.** If the packet has a `deviceid`
byte, the sender must supply `MidiDataType::DeviceId`. Miss one and
`MidiPacket::create` refuses to build the message: a Debug build asserts, and a
**Release build drops the message silently**. Worth checking explicitly when
parameter edits appear to do nothing.

## 7. Verification

Screenshots prove layout. They do not prove wiring, and a control showing a
plausible value is the easiest thing in the world to mistake for a working one.

- Drive the parameter from the MCP tools and read it back. For anything with
  several instances, set each to a **different** value and confirm all of them —
  a staircase down eight rows proves each addresses its own part; one correct
  row proves nothing.
- Flip conditional state both ways and capture each. A dimming rule that happens
  to match the initial state looks identical to one that works.
- `get_state` returns the device's own persistent memory, not the edit buffer.
  On a device whose edit buffer has no sysex address it cannot see live edits at
  all — check the parameter and the UI instead.
- Remember the host is a GUI app: its stdout does not reach a redirect, so
  "no output" is never evidence. Verify by DOM, screenshot or parameter read.
