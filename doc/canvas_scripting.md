# Canvas Scripting

The `<canvas>` element lets skin authors draw custom, data-driven raster graphics — shapes, meters, scopes, envelope displays — directly from Lua. Drawing is done through a **2D context whose API mirrors the HTML5 [`CanvasRenderingContext2D`](https://developer.mozilla.org/en-US/docs/Web/API/CanvasRenderingContext2D)**, so if you have drawn on an HTML canvas before, the drawing code will look familiar.

This guide builds on the [Lua Scripting Guide](/docs/lua-scripting) — read that first for how skin scripts are structured, when they run, and how to access parameters. For the skinning system in general, see the [Skinning Guide](/docs/rmlui-skinning).

## Declaring a canvas

Add a `<canvas>` element to your RML with an `id` and a size. Like any skin element it is positioned with RCSS:

```
<canvas id="myCanvas" style="position: absolute; left: 100dp; top: 100dp; width: 260dp; height: 160dp;"/>
```

## Getting the drawing context

`document:GetElementById` returns a generic element, so cast it to the canvas type with `Element.As.Canvas`, then register a **paint function**. The paint function receives the 2D context (`ctx`) and is called whenever the canvas needs to be (re)drawn:

```lua
local canvas = Element.As.Canvas(document:GetElementById("myCanvas"))

canvas:setPaintFunction(function(ctx)
    ctx.fillStyle = "#2a6bd4"
    ctx:fillRect(8, 8, 100, 60)
end)

canvas:repaint()   -- request the first paint
```

> **Timing:** as explained in the [Lua Scripting Guide](/docs/lua-scripting#script-execution-timing), `<script>` blocks in `<head>` run **before** the body exists. Set up your canvas from a `<body onload="...">` handler (or the document `load` event) so the element is available.

## Quick start

```xml
<rml>
  <head>
    <link type="text/rcss" href="myskin.rcss"/>
    <script>
      function initCanvas()
        local canvas = Element.As.Canvas(document:GetElementById("myCanvas"))
        canvas:setPaintFunction(function(ctx)
          -- filled + outlined rectangle
          ctx.fillStyle = "#2a6bd4"
          ctx:fillRect(8, 8, 100, 60)
          ctx.strokeStyle = "#ffffff"
          ctx.lineWidth = 3
          ctx:strokeRect(8, 8, 100, 60)

          -- filled circle
          ctx.fillStyle = "#e33e3e"
          ctx:beginPath()
          ctx:arc(60, 110, 24, 0, math.pi * 2)
          ctx:fill()

          -- a line
          ctx.strokeStyle = "#ffcc00"
          ctx.lineWidth = 2
          ctx:beginPath()
          ctx:moveTo(8, 150)
          ctx:lineTo(230, 150)
          ctx:stroke()
        end)
        canvas:repaint()
      end
    </script>
  </head>
  <body onload="initCanvas()">
    <canvas id="myCanvas" style="position: absolute; left: 100dp; top: 100dp; width: 260dp; height: 160dp;"/>
  </body>
</rml>
```

## Canvas element methods

| Method | Description |
|--------|-------------|
| `canvas:setPaintFunction(fn)` | Registers the Lua paint function `fn(ctx)`. Replaces any previous one. |
| `canvas:repaint()` | Requests a redraw. Call this whenever the content should change (e.g. from a `params.onChange` callback). |
| `canvas:setClearEveryFrame(enabled)` | If `true` (the default when a paint function is set), the canvas is cleared before each paint. Set `false` for a persistent canvas that you draw onto incrementally. |

## The 2D context

The context passed to your paint function is only valid **during** that call — do not store it and draw later; draw inside the paint function and call `canvas:repaint()` to trigger it.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `ctx.fillStyle` | string | Fill color (CSS color string). Used by `fillRect` and `fill`. |
| `ctx.strokeStyle` | string | Stroke color (CSS color string). Used by `strokeRect` and `stroke`. |
| `ctx.lineWidth` | number | Line thickness in pixels. Used by `strokeRect` and `stroke`. |

### Methods

| Method | Description |
|--------|-------------|
| `ctx:fillRect(x, y, w, h)` | Fills a rectangle with `fillStyle`. |
| `ctx:strokeRect(x, y, w, h)` | Outlines a rectangle with `strokeStyle` / `lineWidth`. |
| `ctx:beginPath()` | Starts a new path. |
| `ctx:closePath()` | Closes the current sub-path back to its start. |
| `ctx:moveTo(x, y)` | Moves the pen to `(x, y)` without drawing. |
| `ctx:lineTo(x, y)` | Adds a line segment to `(x, y)`. |
| `ctx:rect(x, y, w, h)` | Adds a rectangle sub-path to the current path. |
| `ctx:arc(x, y, radius, startAngle, endAngle [, counterclockwise])` | Adds an arc / circle. Angles are in radians. |
| `ctx:ellipse(x, y, radiusX, radiusY, rotation, startAngle, endAngle [, counterclockwise])` | Adds an elliptical arc. |
| `ctx:fill()` | Fills the current path with `fillStyle`. |
| `ctx:stroke()` | Strokes the current path with `strokeStyle` / `lineWidth`. |

Filled and outlined shapes follow the standard HTML5 pattern — rectangles have the `fillRect`/`strokeRect` shortcuts, while circles, ellipses and lines are built as paths and then filled or stroked:

```lua
-- filled circle
ctx.fillStyle = "#e33e3e"
ctx:beginPath()
ctx:arc(cx, cy, r, 0, math.pi * 2)
ctx:fill()

-- outlined ellipse
ctx.strokeStyle = "#1db954"
ctx.lineWidth = 4
ctx:beginPath()
ctx:ellipse(cx, cy, rx, ry, 0, 0, math.pi * 2)
ctx:stroke()
```

## Colors

`fillStyle` and `strokeStyle` accept CSS color strings:

| Form | Example |
|------|---------|
| `#rgb` | `"#f00"` |
| `#rrggbb` | `"#ff0000"` |
| `#rrggbbaa` (with alpha) | `"#ff000080"` |
| `rgb(r, g, b)` | `"rgb(255, 0, 0)"` |
| `rgba(r, g, b, a)` | `"rgba(255, 0, 0, 0.5)"` |

## Coordinate system

Drawing coordinates are in the canvas's **pixel** space with the origin `(0, 0)` at the **top-left** (same as HTML5, and unlike some low-level graphics APIs). Anything drawn outside the canvas's pixel size is clipped. The pixel size follows the element's rendered size, which depends on the skin's scaling — query it if you need to lay out relative to the canvas rather than using fixed coordinates.

## Redrawing from parameter changes

The canvas is only repainted when you ask it to. Combine it with the [Parameter API](/docs/lua-scripting#parameter-api) to make it react to the synth:

```lua
local canvas = Element.As.Canvas(document:GetElementById("cutoffMeter"))

canvas:setPaintFunction(function(ctx)
    local value = params.get("Cutoff")          -- 0..127
    ctx.fillStyle = "#1db954"
    ctx:fillRect(0, 0, value / 127 * 200, 20)
end)

-- repaint whenever the parameter changes
params.onChange("Cutoff", function() canvas:repaint() end)
canvas:repaint()
```

## HTML5 compatibility

The context's property and method names match the HTML5 `CanvasRenderingContext2D`, so drawing code is portable. The one Gearmulator-specific step is the bootstrap: instead of `canvas.getContext("2d")` you register a **paint function** with `setPaintFunction`, and you trigger a redraw with `repaint()`. Everything inside the paint function is standard canvas 2D code.

The current implementation covers rectangles, lines, arcs/circles, ellipses, fill and stroke styles, and line width. More of the HTML5 surface may be added over time.
