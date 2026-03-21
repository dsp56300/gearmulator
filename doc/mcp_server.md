# Gearmulator MCP Server

The Gearmulator MCP (Model Context Protocol) server is embedded in each plugin instance, allowing AI assistants and automation scripts to remotely control the synthesizer, inspect the UI, and run tests.

## Overview

When a Gearmulator plugin is loaded in a DAW, it starts an MCP server on a local TCP port. Any MCP-compatible client can connect to it to:

- Read and write synthesizer parameters
- Send MIDI messages (notes, program changes, SysEx)
- Save and load device state
- Inspect and interact with the plugin UI (DOM tree, clicks, key presses)
- Run automated tests

## Getting Started

### Enabling the MCP Server

The MCP server is enabled by default. To disable it at build time, set the CMake option:

```
-Dgearmulator_BUILD_MCP_SERVER=OFF
```

### Connecting

The server listens on **port 13710** by default. If multiple plugin instances are loaded, each one increments the port automatically (13710, 13711, 13712, ...).

#### Discovery File

Active instances register themselves in a JSON file at:

```
~/.gearmulator_mcp.json
```

This file contains an array of running instances:

```json
[
  {
    "pluginName": "Osirus",
    "plugin4CC": "Osir",
    "port": 13710,
    "pid": 12345
  },
  {
    "pluginName": "Vavra",
    "plugin4CC": "Vavr",
    "port": 13711,
    "pid": 12345
  }
]
```

Use this file to find which port to connect to.

### Transport

The server uses HTTP with Server-Sent Events (SSE):

| Endpoint | Method | Description |
|---|---|---|
| `/sse` | GET | SSE stream for receiving server events |
| `/message` | POST | Send JSON-RPC 2.0 requests |
| `/` | GET | Health check (returns server info) |

### Protocol

All requests use [JSON-RPC 2.0](https://www.jsonrpc.org/specification) over the MCP protocol (version `2024-11-05`).

**Initialize the session:**

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "initialize",
  "params": {
    "protocolVersion": "2024-11-05",
    "clientInfo": { "name": "my-client", "version": "1.0" },
    "capabilities": {}
  }
}
```

**List available tools:**

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "tools/list"
}
```

**Call a tool:**

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "tools/call",
  "params": {
    "name": "get_parameter",
    "arguments": { "name": "Osc1 Shape", "part": 0 }
  }
}
```

## Tools Reference

### Parameters

#### `list_parameters`

List all parameters with their current values, ranges, and metadata for a given part.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `part` | integer | no | Part number (default: 0) |

Returns an array of parameter objects with `name`, `value`, `min`, `max`, `numSteps`, `isBipolar`, `isStringDefined`, `valueText`.

#### `get_parameter`

Get a specific parameter's value and metadata by name.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `name` | string | yes | Parameter name |
| `part` | integer | no | Part number (default: 0) |

#### `set_parameter`

Set a parameter value by name.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `name` | string | yes | Parameter name |
| `value` | number | yes | New parameter value |
| `part` | integer | no | Part number (default: 0) |

#### `set_parameters_batch`

Set multiple parameters at once.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `parameters` | array | yes | Array of `{name, value}` objects |
| `part` | integer | no | Part number (default: 0) |

#### `dump_all_parameters`

Dump all parameter values for all parts as a snapshot for testing/comparison.

No parameters required.

---

### MIDI

#### `send_midi`

Send a raw MIDI message.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `status` | integer | yes | MIDI status byte (0-255) |
| `data1` | integer | yes | First data byte (0-127) |
| `data2` | integer | no | Second data byte (0-127, default: 0) |

#### `send_note`

Send a note on, wait for a duration, then send note off.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `note` | integer | yes | MIDI note number (0-127) |
| `velocity` | integer | no | Note velocity (0-127, default: 100) |
| `channel` | integer | no | MIDI channel (0-15, default: 0) |
| `duration_ms` | integer | no | Note duration in milliseconds (default: 500) |

#### `send_sysex`

Send a SysEx message.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `hex` | string | yes | Hex bytes as a string (e.g. `"F0 00 20 33 ... F7"`) |

#### `send_program_change`

Send a MIDI program change message.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `program` | integer | yes | Program number (0-127) |
| `channel` | integer | no | MIDI channel (0-15, default: 0) |

---

### State

#### `get_state`

Get the current device state as a base64-encoded binary.

No parameters required.

#### `set_state`

Load a device state from base64-encoded binary data.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `data` | string | yes | Base64-encoded state data |

#### `get_current_part`

Get the currently selected part number.

No parameters required.

#### `set_current_part`

Switch the active part.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `part` | integer | yes | Part number to switch to |

---

### Device Info

#### `get_device_info`

Get device information: model, sample rate, channel count, DSP clock, validity.

No parameters required.

#### `get_plugin_info`

Get plugin information: name, version, format identifier.

No parameters required.

---

### DOM Inspection

These tools inspect the RmlUI document tree that makes up the plugin's user interface. They require the plugin editor window to be open.

#### `get_dom_tree`

Get the RmlUI document DOM tree as JSON.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `maxDepth` | integer | no | Maximum tree depth (default: 5, range: 1-50) |
| `rootId` | string | no | Element ID to use as root (default: document root) |

Returns a nested JSON tree with `tag`, `id`, `class`, `attributes`, and `children` for each element.

#### `get_element`

Get detailed information about a specific element by ID.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `id` | string | yes | Element ID |

Returns tag, id, class, attributes, box model (x, y, width, height), visibility, inner RML, and children summary.

#### `find_elements`

Find elements by tag name.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Tag name to search for (e.g. `button`, `input`, `select`) |
| `limit` | integer | no | Maximum results (default: 50, range: 1-500) |

#### `set_element_attribute`

Set an attribute on an element by ID.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `id` | string | yes | Element ID |
| `attribute` | string | yes | Attribute name |
| `value` | string | yes | Attribute value |

---

### UI Input Injection

These tools inject input events through the RmlUI context, identical to real user input. They require the plugin editor window to be open.

#### `click_element`

Simulate a mouse click on an element by ID. Moves the cursor to the element's center, then injects mouse button down and up.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `id` | string | yes | Element ID to click |
| `button` | string | no | `"left"` (default), `"right"`, or `"middle"` |
| `modifiers` | object | no | `{ctrl, shift, alt, meta}` as booleans |

#### `mouse_move`

Move the mouse cursor to specific coordinates or to the center of an element.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `id` | string | no* | Element ID to move to (uses center) |
| `x` | integer | no* | X coordinate in document space |
| `y` | integer | no* | Y coordinate in document space |
| `modifiers` | object | no | `{ctrl, shift, alt, meta}` as booleans |

\* Either `id` or both `x` and `y` must be provided.

#### `mouse_click_at`

Simulate a mouse click at specific coordinates.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `x` | integer | yes | X coordinate |
| `y` | integer | yes | Y coordinate |
| `button` | string | no | `"left"` (default), `"right"`, or `"middle"` |
| `modifiers` | object | no | `{ctrl, shift, alt, meta}` as booleans |

#### `mouse_drag`

Simulate a mouse drag from one position to another with intermediate move events.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `fromId` | string | no* | Element ID to start drag from |
| `fromX` | integer | no* | Start X coordinate |
| `fromY` | integer | no* | Start Y coordinate |
| `toId` | string | no* | Element ID to drag to |
| `toX` | integer | no* | End X coordinate |
| `toY` | integer | no* | End Y coordinate |
| `button` | string | no | `"left"` (default), `"right"`, or `"middle"` |
| `steps` | integer | no | Intermediate move steps (default: 10, range: 1-100) |
| `modifiers` | object | no | `{ctrl, shift, alt, meta}` as booleans |

\* Either `fromId` or both `fromX` and `fromY` must be provided. Same for destination.

#### `mouse_wheel`

Simulate mouse wheel scrolling.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `id` | string | no* | Element ID to scroll on |
| `x` | integer | no* | X coordinate |
| `y` | integer | no* | Y coordinate |
| `deltaX` | number | no | Horizontal scroll delta (default: 0) |
| `deltaY` | number | no | Vertical scroll delta (default: 0). Positive = down. |
| `modifiers` | object | no | `{ctrl, shift, alt, meta}` as booleans |

\* Either `id` or both `x` and `y` must be provided.

#### `send_key`

Simulate a key press, release, or full press+release.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `key` | string | yes | Key name (see table below) |
| `action` | string | no | `"press"` (default, down+up), `"down"`, or `"up"` |
| `modifiers` | object | no | `{ctrl, shift, alt, meta}` as booleans |

**Supported key names:**

| Category | Keys |
|---|---|
| Letters | `a` through `z` |
| Digits | `0` through `9` |
| Navigation | `left`, `right`, `up`, `down`, `home`, `end`, `pageup`, `pagedown` |
| Editing | `backspace`, `delete`, `insert`, `tab`, `space`, `return` / `enter`, `escape` / `esc` |
| Function | `f1` through `f12` |

#### `send_text`

Inject text input into the currently focused element, character by character.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `text` | string | yes | Text to inject |

---

## Examples

### Read a parameter value

```json
{
  "jsonrpc": "2.0", "id": 1,
  "method": "tools/call",
  "params": {
    "name": "get_parameter",
    "arguments": { "name": "Osc1 Shape", "part": 0 }
  }
}
```

### Sweep a filter cutoff

```json
{
  "jsonrpc": "2.0", "id": 2,
  "method": "tools/call",
  "params": {
    "name": "set_parameter",
    "arguments": { "name": "Filter1 Cutoff", "value": 64, "part": 0 }
  }
}
```

### Play a note

```json
{
  "jsonrpc": "2.0", "id": 3,
  "method": "tools/call",
  "params": {
    "name": "send_note",
    "arguments": { "note": 60, "velocity": 100, "duration_ms": 1000 }
  }
}
```

### Right-click a UI element

```json
{
  "jsonrpc": "2.0", "id": 4,
  "method": "tools/call",
  "params": {
    "name": "click_element",
    "arguments": { "id": "osc1_shape", "button": "right" }
  }
}
```

### Drag a slider

```json
{
  "jsonrpc": "2.0", "id": 5,
  "method": "tools/call",
  "params": {
    "name": "mouse_drag",
    "arguments": {
      "fromId": "cutoff_slider",
      "toX": 200, "toY": 50,
      "steps": 20
    }
  }
}
```

### Type into a text field

```json
{
  "jsonrpc": "2.0", "id": 6,
  "method": "tools/call",
  "params": {
    "name": "click_element",
    "arguments": { "id": "patch_name_input" }
  }
}
```

```json
{
  "jsonrpc": "2.0", "id": 7,
  "method": "tools/call",
  "params": {
    "name": "send_text",
    "arguments": { "text": "My Patch" }
  }
}
```

### Inspect the DOM

```json
{
  "jsonrpc": "2.0", "id": 8,
  "method": "tools/call",
  "params": {
    "name": "get_dom_tree",
    "arguments": { "maxDepth": 3 }
  }
}
```

## Architecture

```
┌─────────────────────────────────────────────┐
│  DAW Host                                   │
│  ┌────────────────────────────────────────┐ │
│  │  Plugin Instance (e.g. Osirus VST3)    │ │
│  │  ┌──────────────┐  ┌────────────────┐  │ │
│  │  │ Synth Engine  │  │  MCP Server    │  │ │
│  │  │ (DSP + MIDI)  │  │  (HTTP + SSE)  │  │ │
│  │  └──────┬───────┘  └───────┬────────┘  │ │
│  │         │                  │            │ │
│  │  ┌──────┴──────────────────┴────────┐  │ │
│  │  │  Plugin Processor                │  │ │
│  │  │  (Parameters, State, Controller) │  │ │
│  │  └──────────────┬──────────────────┘   │ │
│  │                 │                      │ │
│  │  ┌──────────────┴──────────────────┐   │ │
│  │  │  RmlUI Editor (DOM + Rendering) │   │ │
│  │  └─────────────────────────────────┘   │ │
│  └────────────────────────────────────────┘ │
└─────────────────────────────────────────────┘
         │
         │ TCP (port 13710+)
         │
┌────────┴────────┐
│  MCP Client     │
│  (AI assistant, │
│   test script)  │
└─────────────────┘
```

## Thread Safety

- **Parameter and MIDI tools** run on the network thread. Parameter access is thread-safe through the JUCE parameter system.
- **DOM and UI input tools** dispatch to the JUCE message thread via `std::promise`/`std::future` and acquire the RmlUI mutex (`ScopedAccess`) before touching the DOM.
- The MCP server never runs on the audio thread.

## Limitations

- DOM and UI input tools require the plugin editor window to be open. They return a clear error if the window is closed.
- The discovery file may contain stale entries if a plugin crashes without cleanup. Entries include the process ID (`pid`) so clients can verify liveness.
- Maximum of 100 simultaneous plugin instances (ports 13710–13809).
