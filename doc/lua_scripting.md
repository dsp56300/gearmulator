# Lua Scripting for Gearmulator Skins

Gearmulator integrates Lua 5.4 into the RmlUi skin framework, allowing skin authors to create dynamic UI behavior without C++ code. Lua scripts can read and write synth parameters, react to parameter changes, and manipulate the DOM.

For full RmlUi documentation (RML elements, RCSS properties, data bindings, events), see the official RmlUi documentation at https://mikke89.github.io/RmlUi/

## Quick Start

Add a `<script>` block in your RML file's `<head>` section:

```html
<rml>
  <head>
    <link type="text/rcss" href="myskin.rcss"/>
    <script>
      function onBodyLoad()
        local val = params.get("Osc1Pitch")
        Log.Message(Log.logtype.info, "Osc1Pitch = " .. val)
      end
    </script>
  </head>
  <body onload="onBodyLoad()">
    ...
  </body>
</rml>
```

### Script Execution Timing

- `<script>` blocks in `<head>` run during document parsing, **before** `<body>` elements exist. Define functions here but do not access DOM elements.
- Use `onload="myFunction()"` on `<body>` to run code after all elements are created but before layout is computed.
- Use `onshow="myFunction()"` on an element to run code when that element becomes visible. Layout is computed at this point, so `offset_width`/`offset_height` return correct values.
- Inline event handlers like `onclick="..."` receive `event`, `element`, and `document` as parameters.

### Globals

| Global | Type | Description |
|--------|------|-------------|
| `document` | ElementDocument | The current RML document. Available in `<script>` blocks and event handlers. |
| `params` | table | Parameter API for reading/writing synth parameters and subscribing to changes. |
| `Log` | table | RmlUi logging. Use `Log.Message(Log.logtype.info, "message")`. |
| `rmlui` | table | RmlUi core API (contexts, font loading, etc.). |

## Parameter API (`params`)

### Reading Values

```lua
-- Get raw integer value (current part)
local value = params.get("Osc1Pitch")

-- Get for a specific part
local value = params.get("Osc1Pitch", 3)

-- Get for explicit "current" part
local value = params.get("Osc1Pitch", "current")

-- Get formatted display text
local text = params.getText("Osc1Pitch")
local text = params.getText("Osc1Pitch", 3)
```

The part argument is optional for all functions. When omitted, the current part is used.

### Writing Values

```lua
-- Set value on current part
params.set("Osc1Pitch", 64)

-- Set value on specific part
params.set("Osc1Pitch", 64, 3)
```

### Parameter Info

```lua
local info = params.getInfo("Osc1Pitch")
-- info.min          (integer) minimum value
-- info.max          (integer) maximum value
-- info.name         (string)  internal parameter name
-- info.displayName  (string)  human-readable name
-- info.defaultValue (integer) default value
-- info.isBool       (boolean) true if parameter is a toggle
-- info.isBipolar    (boolean) true if parameter is bipolar
```

### Subscribing to Changes

```lua
-- Subscribe to changes on current part (auto-rebinds when part changes)
local id = params.onChange("Osc1Pitch", function(value, text)
  -- value: raw integer
  -- text: formatted display string
end)

-- Subscribe to changes on a specific part
local id = params.onChange("Osc1Pitch", function(value, text)
  -- ...
end, 3)

-- Unsubscribe
params.removeListener(id)
```

When subscribing with the current part (default), the listener automatically rebinds to the new part's parameter when the current part changes. If the value differs between the old and new part, the callback fires immediately with the new value.

### Current Part

```lua
-- Get current part number (0-based)
local part = params.getCurrentPart()

-- Subscribe to part changes
local id = params.onPartChanged(function(newPart)
  -- newPart: 0-based part number
end)
```

## DOM Manipulation

The `document` global provides access to the RmlUi DOM:

```lua
-- Find elements
local elem = document:GetElementById("myElement")

-- Read/write styles
elem.style.width = "100px"
elem.style.height = "50px"
elem.style["background-color"] = "#ff0000"

-- Read computed size (in px, available after layout)
local w = elem.offset_width
local h = elem.offset_height

-- Add/remove CSS classes
elem:SetClass("active", true)
elem:SetClass("hidden", false)

-- Read/write attributes
local val = elem:GetAttribute("id")
elem:SetAttribute("data-value", "42")

-- Read/write inner RML
elem.inner_rml = '<div class="child">Hello</div>'

-- Navigate
local parent = elem.parent_node
local first = elem.first_child
local doc = elem.owner_document
```

## Logging

```lua
Log.Message(Log.logtype.info, "informational message")
Log.Message(Log.logtype.warning, "warning message")
Log.Message(Log.logtype.error, "error message")
```

Log levels: `always`, `error`, `warning`, `info`, `debug`.

## Example: Arpeggiator User Pattern Visualization

This example creates a bar graph showing the arpeggiator step pattern, with bar width controlled by step length and height by step velocity:

**RML (in `<head>`):**
```html
<style>
  .arp-bar {
    position: absolute;
    bottom: 0px;
    background-color: #ffffffdd;
  }
  .arp-bar-even { background-color: #ccddffcc; }
  .arp-bar-odd  { background-color: #ddccffcc; }
  .arp-bar-inactive { background-color: #aaaaaa55; }
</style>
<script>
  local STEPS = 32
  local bars = {}
  local container = nil

  function updateArpBar(step)
    local bar = bars[step]
    if bar == nil or container == nil then return end

    local cw = container.offset_width
    local ch = container.offset_height
    if cw <= 0 or ch <= 0 then return end
    local stepMaxW = cw / STEPS

    local length   = params.get("Step " .. step .. " Length")
    local velocity = params.get("Step " .. step .. " Velocity")
    local bitfield = params.get("Step " .. step .. " Bitfield")
    local patLen   = params.get("Arpeggiator/UserPatternLength")

    local active = (step - 1) <= patLen and bitfield > 0

    bar.style.left   = math.floor((step - 1) * stepMaxW) .. "px"
    bar.style.width  = math.floor((length / 127) * stepMaxW) .. "px"
    bar.style.height = math.floor((velocity / 127) * ch) .. "px"

    bar:SetClass("arp-bar-inactive", not active)
  end

  function updateAllArpBars()
    for i = 1, STEPS do updateArpBar(i) end
  end

  function initArp()
    container = document:GetElementById("LuaArpUserGraphics")
    for i = 1, STEPS do
      bars[i] = document:GetElementById("arp-bar-" .. i)
    end
    for i = 1, STEPS do
      local step = i
      params.onChange("Step " .. step .. " Length",    function() updateArpBar(step) end)
      params.onChange("Step " .. step .. " Velocity",  function() updateArpBar(step) end)
      params.onChange("Step " .. step .. " Bitfield",  function() updateArpBar(step) end)
    end
    params.onChange("Arpeggiator/UserPatternLength", function() updateAllArpBars() end)
  end
</script>
```

**RML (in `<body>`):**
```html
<body onload="initArp()">
  ...
  <div id="LuaArpUserGraphics" onshow="updateAllArpBars()"
       style="width: 1921dp; height: 476dp; position: relative; overflow: hidden;">
    <div id="arp-bar-1"  class="arp-bar arp-bar-odd"/>
    <div id="arp-bar-2"  class="arp-bar arp-bar-even"/>
    <!-- ... bars 3-31 ... -->
    <div id="arp-bar-32" class="arp-bar arp-bar-even"/>
  </div>
  ...
</body>
```

Key patterns used:
- `onload` on `<body>` for initialization (element lookup + parameter subscriptions)
- `onshow` on the container for initial/repeated rendering (layout is computed at this point)
- `offset_width`/`offset_height` for size-independent layout (values are in px)
- `params.onChange` with default current part for automatic part tracking

## Debugging

### RmlUi Debugger

Gearmulator includes the RmlUi visual debugger. To enable it, open the plugin's settings and enable it in the Skin section. The debugger overlay will appear immediately.

The debugger provides:

- **Element Info** - Inspect any element's computed styles, box model, attributes, and classes by clicking on it.
- **DOM Tree** - Browse the full document hierarchy to verify that Lua-created or Lua-modified elements are structured correctly.
- **Event Log** - View all RmlUi log messages, including Lua errors, warnings, and `Log.Message` output. This is the primary way to see Lua debug output and diagnose script errors.

### Debug Logging from Lua

Use `Log.Message` to write to the RmlUi event log:

```lua
Log.Message(Log.logtype.info, "debug: value = " .. tostring(myVar))
```

These messages appear in the debugger's Event Log panel. Lua errors (syntax errors, runtime errors, timeouts) are also logged there automatically with full stack tracebacks.

## Safety

### Sandboxed Standard Libraries

Only safe Lua standard libraries are loaded: `base`, `coroutine`, `table`, `string`, `math`, `utf8`. The following are **not available**:

| Library | Reason |
|---------|--------|
| `io` | Filesystem access |
| `os` | Shell commands, file operations |
| `package` | Loading arbitrary native code via `require` |
| `debug` | Internal state inspection/modification |

### Execution Timeout

Scripts are limited to 3 seconds of execution time. If a script exceeds this limit (e.g., an infinite loop), it is terminated with an error message and the plugin continues normally. The timeout applies to all Lua execution: inline scripts, external scripts, event handlers, and parameter callbacks.

### Error Handling

All Lua errors are caught via protected calls (`lua_pcall`). Errors are logged as warnings through the RmlUi logging system and never crash the plugin or DAW. A full stack traceback is included in the error message.

## Adding Lua Files to Skins

Place `.lua` files in the skin folder alongside `.rml`, `.rcss`, and image files. They are automatically compiled into the plugin's binary data and can be loaded from RML:

```html
<script src="myskin.lua"></script>
```
