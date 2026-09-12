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

| Property | Type | Description |
|----------|------|-------------|
| `canvas.width` | number | Width of the drawing area in pixels. Read-only. |
| `canvas.height` | number | Height of the drawing area in pixels. Read-only. |

## The 2D context

The context passed to your paint function is only valid **during** that call — do not store it and draw later; draw inside the paint function and call `canvas:repaint()` to trigger it.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `ctx.fillStyle` | string or gradient | Fill color (CSS color string) or a gradient object. Used by `fillRect` and `fill`. |
| `ctx.strokeStyle` | string or gradient | Stroke color (CSS color string) or a gradient object. Used by `strokeRect` and `stroke`. |
| `ctx.lineWidth` | number | Line thickness in pixels. Used by `strokeRect` and `stroke`. |
| `ctx.lineCap` | string | Cap drawn at the end of an open sub-path: `"butt"` (the default), `"round"` or `"square"`. |
| `ctx.lineJoin` | string | Join drawn where two segments meet: `"miter"` (the default), `"round"` or `"bevel"`. |
| `ctx.lineDashOffset` | number | Distance into the dash pattern at which a stroke starts. |
| `ctx.globalAlpha` | number | Opacity from 0 to 1, multiplied into every fill, stroke and shadow. |
| `ctx.shadowColor` | string | Shadow color (CSS color string). Fully transparent (the default) means no shadow. |
| `ctx.shadowBlur` | number | Shadow blur width in pixels. `0` (the default) gives a hard-edged shadow. |
| `ctx.shadowOffsetX` | number | Horizontal shadow displacement in pixels. Not affected by the transform. |
| `ctx.shadowOffsetY` | number | Vertical shadow displacement in pixels. Not affected by the transform. |
| `ctx.canvas` | canvas | The canvas being drawn into, whose `width` and `height` give the drawing size. Read-only. |

### Methods

| Method | Description |
|--------|-------------|
| `ctx:save()` | Pushes the drawing state (styles, transform and clip) onto a stack. |
| `ctx:restore()` | Pops the last saved state. The current path is not part of it. |
| `ctx:translate(x, y)` | Moves the origin by `(x, y)`. |
| `ctx:rotate(angle)` | Rotates about the origin. Angle is in radians. |
| `ctx:scale(x, y)` | Scales the axes by `x` and `y`. |
| `ctx:transform(a, b, c, d, e, f)` | Multiplies the current transform by the given matrix. |
| `ctx:setTransform(a, b, c, d, e, f)` | Replaces the current transform with the given matrix. |
| `ctx:resetTransform()` | Resets the transform to the identity. |
| `ctx:clearRect(x, y, w, h)` | Erases a rectangle back to transparent. Unlike the other calls it works straight on the canvas pixels, ignoring the transform and the clip. |
| `ctx:fillRect(x, y, w, h)` | Fills a rectangle with `fillStyle`. |
| `ctx:strokeRect(x, y, w, h)` | Outlines a rectangle with `strokeStyle` / `lineWidth`. |
| `ctx:beginPath()` | Starts a new path. |
| `ctx:closePath()` | Closes the current sub-path back to its start. |
| `ctx:moveTo(x, y)` | Moves the pen to `(x, y)` without drawing. |
| `ctx:lineTo(x, y)` | Adds a line segment to `(x, y)`. |
| `ctx:quadraticCurveTo(cx, cy, x, y)` | Adds a quadratic curve to `(x, y)` with one control point. |
| `ctx:bezierCurveTo(c1x, c1y, c2x, c2y, x, y)` | Adds a cubic curve to `(x, y)` with two control points. |
| `ctx:rect(x, y, w, h)` | Adds a rectangle sub-path to the current path. |
| `ctx:roundRect(x, y, w, h [, radius])` | Adds a rectangle sub-path with rounded corners. `radius` may also be a table; its first entry is used. |
| `ctx:arc(x, y, radius, startAngle, endAngle [, counterclockwise])` | Adds an arc / circle. Angles are in radians. |
| `ctx:arcTo(x1, y1, x2, y2, radius)` | Adds an arc of `radius` fitted into the corner between the current point, `(x1, y1)` and `(x2, y2)`. |
| `ctx:ellipse(x, y, radiusX, radiusY, rotation, startAngle, endAngle [, counterclockwise])` | Adds an elliptical arc. |
| `ctx:fill([fillRule])` | Fills the current path with `fillStyle`. `fillRule` is `"nonzero"` (the default) or `"evenodd"`. |
| `ctx:stroke()` | Strokes the current path with `strokeStyle` / `lineWidth`. |
| `ctx:clip([fillRule])` | Narrows the clip region to the current path. Undone by `restore`. |
| `ctx:isPointInPath(x, y [, fillRule])` | Returns `true` if `(x, y)` lies inside the current path. |
| `ctx:isPointInStroke(x, y)` | Returns `true` if `(x, y)` lies inside the outline `stroke` would paint. |
| `ctx:setLineDash(lengths)` | Sets the dash pattern from a table of alternating on / off lengths. An empty table draws a solid line. |
| `ctx:getLineDash()` | Returns the current dash pattern as a table. |

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

## Gradients

A gradient is created on the context, given its stops, and then assigned to `fillStyle` or `strokeStyle` in place of a color string. Its coordinates are in the same space as the drawing, so it follows the current transform:

| Method | Description |
|--------|-------------|
| `ctx:createLinearGradient(x0, y0, x1, y1)` | Returns a gradient ramping along the line between the two points. |
| `ctx:createRadialGradient(x0, y0, r0, x1, y1, r1)` | Returns a gradient ramping between the two circles. |
| `ctx:createConicGradient(startAngle, x, y)` | Returns a gradient sweeping around `(x, y)` from `startAngle`. |
| `gradient:addColorStop(offset, color)` | Adds a stop at `offset` (0 at the start of the ramp, 1 at its end). Raises an error if `offset` is outside that range or `color` does not parse. |

```lua
local ramp = ctx:createLinearGradient(0, 0, 200, 0)
ramp:addColorStop(0, "#1db954")
ramp:addColorStop(1, "#e33e3e")

ctx.fillStyle = ramp
ctx:fillRect(0, 0, 200, 20)
```

## Colors

`fillStyle`, `strokeStyle`, `shadowColor`, and `addColorStop` all accept CSS color strings:

| Form | Example |
|------|---------|
| `#rgb` | `"#f00"` |
| `#rrggbb` | `"#ff0000"` |
| `#rrggbbaa` (with alpha) | `"#ff000080"` |
| `rgb(r, g, b)` | `"rgb(255, 0, 0)"` |
| `rgba(r, g, b, a)` | `"rgba(255, 0, 0, 0.5)"` |
| `hsl(h, s%, l%)` / `hsla(h, s%, l%, a)` | `"hsl(0, 100%, 50%)"` |
| named color | `"red"` (note: only 19 named colors available) |

## Coordinate system

Drawing coordinates are in the canvas's pixel space with the origin (0, 0) at the top-left (same as HTML5, and unlike some low-level graphics APIs). Anything drawn outside the canvas's pixel size is clipped. The pixel size follows the element's rendered size, which depends on the skin's scaling. Without adjusting for this, canvas contents won't follow the skin scaling:

```lua
canvas:setPaintFunction(function(ctx)
    ctx.fillStyle = "#1db954"
    ctx:fillRect(0, 0, 100, 200)     -- always 100 x 200 pixels on the user's screen
end)
```

Query the canvas's rendered size if you need to lay out relative to the canvas rather than using fixed coordinates:

```lua
canvas:setPaintFunction(function(ctx)
    local w, h = ctx.canvas.width, ctx.canvas.height
    ctx.fillStyle = "#1db954"
    ctx:fillRect(0, 0, w * 0.5, h)   -- always half the canvas, at any scaling
end)
```

Alternatively, use `ctx:scale` to rescale the canvas's coordinate system if you want to work with fixed coordinates that still scale properly:

```lua
canvas:setPaintFunction(function(ctx)
    ctx:scale(ctx.canvas.width / 200, ctx.canvas.height / 200)
    ctx.fillStyle = "#1db954"
    ctx:fillRect(0, 0, 100, 200)   -- always half the canvas, at any scaling
end)
```

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

The implementation covers the drawing half of the HTML5 surface: paths, transforms, clipping, hit testing, line dashes, shadows and gradients. `globalCompositeOperation`, `miterLimit` and `filter` are not currently implemented due to complexity or JUCE limitations. Text and image methods are also not implemented, as they can be better handled using RmlUi.
