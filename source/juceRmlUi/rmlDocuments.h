#pragma once

namespace juceRmlUi
{
constexpr auto g_rmlDocSimple = R"(
<rml>
<head>
    <style>
        body { background-color: #2e2e2e; color: white; font-family: Bebas Neue; margin: 20px; }
        h1 { color: #ffcc00; font-size: 64px; }
		p { font-size: 40px; }
    </style>
</head>
<body>
    <h1>Hello from RmlUi inside JUCE!</h1>
    <p>This is a test interface.</p>
</body>
</rml>
)";

constexpr auto g_rmlDoc2 = R"(
<rml>
<head>
	<style>
		body {
			font-family: "Bebas Neue";
			font-size: 16px;
			color: #eee;
			background-color: #202020;
			padding: 20px;
			width: 100%;
		}
		h1 {
			font-size: 48px;
			color: #ffcc00;
			text-align: center;
			margin-bottom: 20px;
		}
		h2 {
			font-size: 32px;
			color: #66ccff;
			margin-top: 30px;
			border-bottom: 1px solid #444;
			padding-bottom: 5px;
		}
		p {
			font-size: 20px;
			color: #dddddd;
			line-height: 1.6;
			margin-bottom: 1em;
		}
		.highlight {
			color: #ff8888;
			font-weight: bold;
		}
		.box {
			background-color: #333;
			border: 2px solid #555;
			padding: 10px;
			margin-top: 20px;
			border-radius: 10px;
		}
		.flex-row {
			display: flex;
			flex-direction: row;
			justify-content: space-around;
			flex-wrap: wrap;
			gap: 10px;
			margin-top: 20px;
		}
		.flex-item {
			background-color: #444;
			padding: 15px;
			color: white;
			border-radius: 6px;
			width: 30%;
			min-width: 200px;
			text-align: center;
			box-sizing: border-box;
		}
	</style>
</head>
<body>
	<h1>Welcome to RmlUi</h1>

	<p>This example demonstrates <span class="highlight">inline styling</span>, layout features, and text wrapping. Resize the window to test how content adapts to narrower viewports.</p>

	<h2>Styled Box</h2>
	<div class="box">
		<p>This is a box with a background color, padding, and rounded corners. It wraps its content properly and adapts if the text is long enough to need multiple lines.</p>
	</div>

	<h2>Flex Layout</h2>
	<div class="flex-row">
		<div class="flex-item">Column 1 with some longer text that should wrap nicely in narrow windows.</div>
		<div class="flex-item">Column 2 also has a lot of text. Let's see how it behaves on resize!</div>
		<div class="flex-item">Column 3: less text here.</div>
	</div>

	<h2>More Text</h2>
	<p>RmlUi allows extensive control over layout using familiar CSS-like rules. Text naturally wraps inside paragraphs and block containers, just like in HTML. You can build flexible UI panels, popups, HUD elements, or even in-game UIs entirely with markup.</p>
</body>
</rml>
)";

constexpr auto g_rmlDocMouseTest = R"(
<rml>
<head>
    <style>
        body {
            font-family: Bebas Neue;
            font-size: 16px;
            padding: 40px;
            background-color: #222;
            color: white;
        }

        #button {
            width: 200px;
            height: 60px;
            background-color: #555;
            text-align: center;
            line-height: 60px;
            border-radius: 8px;
            transition: background-color 0.5s elastic-out;
        }

        #button:hover {
            background-color: #2a9d8f;
        }

        #button:active {
            background-color: #e76f51;
        }

        #info {
            margin-top: 20px;
            font-size: 14px;
            color: #bbb;
        }
    </style>
</head>
<body>
    <div id="button">Hover or Click Me</div>
    <div id="info">This is a pure RML/CSS test. No scripting, no C++.</div>
</body>
</rml>
)";
}