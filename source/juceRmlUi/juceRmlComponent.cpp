#include "juceRmlComponent.h"

#include <cassert>

#include "rmlFileInterface.h"
#include "rmlRenderInterface.h"
#include "rmlSystemInterface.h"
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/Core.h"
#include "RmlUi/Core/ElementDocument.h"

#include "juceUiLib/editor.h"

namespace juceRmlUi
{
	RmlComponent::RmlComponent(genericUI::Editor& _editor) : m_editor(_editor)
	{
		m_openGLContext.setMultisamplingEnabled(true);
		m_openGLContext.setRenderer(this);
		m_openGLContext.attachTo(*this);
		m_openGLContext.setContinuousRepainting(true);
//		m_openGLContext.setSwapInterval(1);
//		m_openGLContext.setComponentPaintingEnabled(true);
	}

	RmlComponent::~RmlComponent()
	{
		m_openGLContext.detach();
	}

	void RmlComponent::newOpenGLContextCreated()
	{
		SetSystemInterface(new SystemInterface());

		m_renderInterface = new RenderInterface();

		SetFileInterface(new FileInterface(m_editor.getInterface()));

		Rml::Initialise();

		Rml::LoadFontFace("BEBASNEUE_BOLD-_1_.ttf", true);
//		Rml::LoadFontFace("BEBASNEUE_REGULAR-_1_.ttf", true);

		auto size = getLocalBounds();

		m_rmlContext = CreateContext(getName().toStdString(), {size.getWidth(), size.getHeight()},
		                                           m_renderInterface, nullptr);

		const char* doc = R"(
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

		const char* doc2 = R"(
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

        auto* document = m_rmlContext->LoadDocumentFromMemory(doc2);
        if (document)
	        document->Show();
		else
			assert(false);
	}

	void RmlComponent::renderOpenGL()
	{
		juce::OpenGLHelpers::clear(juce::Colours::grey);

		using namespace juce::gl;

		auto contextDims = m_rmlContext->GetDimensions();

		int width = getScreenBounds().getWidth();
		int height = getScreenBounds().getHeight();

		if (contextDims.x != width || contextDims.y != height)
		{
			m_rmlContext->SetDimensions({ width, height });
		}

		glViewport(0, 0, width, height);

		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0, width, height, 0, -1, 1);

		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_LIGHTING);
		glDisable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		if (!m_rmlContext)
			return;
		m_rmlContext->Update();
		m_rmlContext->Render();
		/*
		float verts[] = {
		    100.f, 200.f,
		    700.f, 100.f,
		    700.f, 800.f,
		    100.f, 800.f
		};
		uint32_t indices[] = { 0, 1, 2, 0, 2, 3 };

		glDisableClientState(GL_COLOR_ARRAY);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glEnableClientState(GL_VERTEX_ARRAY);
		glColor4f(1,0,1,1);
		glVertexPointer(2, GL_FLOAT, 0, verts);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, indices);
		*/
    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
        DBG("OpenGL error: " << juce::String::toHexString((int)err));
	}

	void RmlComponent::openGLContextClosing()
	{
	}

	void RmlComponent::resized()
	{
		Component::resized();

		if (m_rmlContext)
			m_rmlContext->SetDimensions({ getWidth(), getHeight() });
	}
}
