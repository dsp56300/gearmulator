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

		setWantsKeyboardFocus(true);
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

	namespace
	{
		int toRmlModifiers(const juce::ModifierKeys _mods)
		{
			int rmlModifiers = 0;
			if (_mods.isCtrlDown())
				rmlModifiers |= Rml::Input::KeyModifier::KM_CTRL;
			if (_mods.isShiftDown())
				rmlModifiers |= Rml::Input::KeyModifier::KM_SHIFT;
			if (_mods.isAltDown())
				rmlModifiers |= Rml::Input::KeyModifier::KM_ALT;
			if (_mods.isCommandDown())
				rmlModifiers |= Rml::Input::KeyModifier::KM_META;
			return rmlModifiers;
		}
		int toRmlModifiers(const juce::MouseEvent& _e)
		{
			return toRmlModifiers(_e.mods);
		}
		int toRmlModifiers(const juce::KeyPress& _e)
		{
			return toRmlModifiers(_e.getModifiers());
		}
		int toRmlMouseButton(const juce::MouseEvent& _e)
		{
			if (_e.mods.isRightButtonDown())
				return 1;
			if (_e.mods.isMiddleButtonDown())
				return 2;
			return 0;
		}
		Rml::Input::KeyIdentifier toRmlKey(const juce::KeyPress& _key)
		{
			switch (_key.getKeyCode())
			{
			case juce::KeyPress::spaceKey: return Rml::Input::KI_SPACE;
			case juce::KeyPress::returnKey: return Rml::Input::KI_RETURN;
			case juce::KeyPress::escapeKey: return Rml::Input::KI_ESCAPE;
			case juce::KeyPress::backspaceKey: return Rml::Input::KI_BACK;
			case juce::KeyPress::deleteKey: return Rml::Input::KI_DELETE;
			case juce::KeyPress::insertKey: return Rml::Input::KI_INSERT;
			case juce::KeyPress::tabKey: return Rml::Input::KI_TAB;
			case juce::KeyPress::leftKey: return Rml::Input::KI_LEFT;
			case juce::KeyPress::rightKey: return Rml::Input::KI_RIGHT;
			case juce::KeyPress::upKey: return Rml::Input::KI_UP;
			case juce::KeyPress::downKey: return Rml::Input::KI_DOWN;
			case juce::KeyPress::homeKey: return Rml::Input::KI_HOME;
			case juce::KeyPress::endKey: return Rml::Input::KI_END;
			case juce::KeyPress::pageUpKey: return Rml::Input::KI_PRIOR;
			case juce::KeyPress::pageDownKey: return Rml::Input::KI_NEXT;
			case juce::KeyPress::F1Key: return Rml::Input::KI_F1;
			case juce::KeyPress::F2Key: return Rml::Input::KI_F2;
			case juce::KeyPress::F3Key: return Rml::Input::KI_F3;
			case juce::KeyPress::F4Key: return Rml::Input::KI_F4;
			case juce::KeyPress::F5Key: return Rml::Input::KI_F5;
			case juce::KeyPress::F6Key: return Rml::Input::KI_F6;
			case juce::KeyPress::F7Key: return Rml::Input::KI_F7;
			case juce::KeyPress::F8Key: return Rml::Input::KI_F8;
			case juce::KeyPress::F9Key: return Rml::Input::KI_F9;
			case juce::KeyPress::F10Key: return Rml::Input::KI_F10;
			case juce::KeyPress::F11Key: return Rml::Input::KI_F11;
			case juce::KeyPress::F12Key: return Rml::Input::KI_F12;
			case juce::KeyPress::F13Key: return Rml::Input::KI_F13;
			case juce::KeyPress::F14Key: return Rml::Input::KI_F14;
			case juce::KeyPress::F15Key: return Rml::Input::KI_F15;
			case juce::KeyPress::F16Key: return Rml::Input::KI_F16;
			case juce::KeyPress::F17Key: return Rml::Input::KI_F17;
			case juce::KeyPress::F18Key: return Rml::Input::KI_F18;
			case juce::KeyPress::F19Key: return Rml::Input::KI_F19;
			case juce::KeyPress::F20Key: return Rml::Input::KI_F20;
			case juce::KeyPress::F21Key: return Rml::Input::KI_F21;
			case juce::KeyPress::F22Key: return Rml::Input::KI_F22;
			case juce::KeyPress::F23Key: return Rml::Input::KI_F23;
			case juce::KeyPress::F24Key: return Rml::Input::KI_F24;
			case juce::KeyPress::numberPad0: return Rml::Input::KI_NUMPAD0;
			case juce::KeyPress::numberPad1: return Rml::Input::KI_NUMPAD1;
			case juce::KeyPress::numberPad2: return Rml::Input::KI_NUMPAD2;
			case juce::KeyPress::numberPad3: return Rml::Input::KI_NUMPAD3;
			case juce::KeyPress::numberPad4: return Rml::Input::KI_NUMPAD4;
			case juce::KeyPress::numberPad5: return Rml::Input::KI_NUMPAD5;
			case juce::KeyPress::numberPad6: return Rml::Input::KI_NUMPAD6;
			case juce::KeyPress::numberPad7: return Rml::Input::KI_NUMPAD7;
			case juce::KeyPress::numberPad8: return Rml::Input::KI_NUMPAD8;
			case juce::KeyPress::numberPad9: return Rml::Input::KI_NUMPAD9;
			case juce::KeyPress::numberPadAdd: return Rml::Input::KI_ADD;
			case juce::KeyPress::numberPadSubtract: return Rml::Input::KI_SUBTRACT;
			case juce::KeyPress::numberPadMultiply: return Rml::Input::KI_MULTIPLY;
			case juce::KeyPress::numberPadDivide: return Rml::Input::KI_DIVIDE;
			case juce::KeyPress::numberPadSeparator: return Rml::Input::KI_SEPARATOR;
			case juce::KeyPress::numberPadDecimalPoint: return Rml::Input::KI_DECIMAL;
			case juce::KeyPress::numberPadEquals: return Rml::Input::KI_OEM_NEC_EQUAL;
			case juce::KeyPress::numberPadDelete: return Rml::Input::KI_DELETE;
			case juce::KeyPress::playKey: return Rml::Input::KI_PLAY;
			case juce::KeyPress::stopKey: return Rml::Input::KI_MEDIA_STOP;
			case juce::KeyPress::fastForwardKey: return Rml::Input::KI_MEDIA_NEXT_TRACK;
			case juce::KeyPress::rewindKey: return Rml::Input::KI_MEDIA_PREV_TRACK;
			case '0':	return Rml::Input::KI_0;
			case '1':	return Rml::Input::KI_1;
			case '2':	return Rml::Input::KI_2;
			case '3':	return Rml::Input::KI_3;
			case '4':	return Rml::Input::KI_4;
			case '5':	return Rml::Input::KI_5;
			case '6':	return Rml::Input::KI_6;
			case '7':	return Rml::Input::KI_7;
			case '8':	return Rml::Input::KI_8;
			case '9':	return Rml::Input::KI_9;
			case 'a':	return Rml::Input::KI_A;
			case 'b':	return Rml::Input::KI_B;
			case 'c':	return Rml::Input::KI_C;
			case 'd':	return Rml::Input::KI_D;
			case 'e':	return Rml::Input::KI_E;
			case 'f':	return Rml::Input::KI_F;
			case 'g':	return Rml::Input::KI_G;
			case 'h':	return Rml::Input::KI_H;
			case 'i':	return Rml::Input::KI_I;
			case 'j':	return Rml::Input::KI_J;
			case 'k':	return Rml::Input::KI_K;
			case 'l':	return Rml::Input::KI_L;
			case 'm':	return Rml::Input::KI_M;
			case 'n':	return Rml::Input::KI_N;
			case 'o':	return Rml::Input::KI_O;
			case 'p':	return Rml::Input::KI_P;
			case 'q':	return Rml::Input::KI_Q;
			case 'r':	return Rml::Input::KI_R;
			case 's':	return Rml::Input::KI_S;
			case 't':	return Rml::Input::KI_T;
			case 'u':	return Rml::Input::KI_U;
			case 'v':	return Rml::Input::KI_V;
			case 'w':	return Rml::Input::KI_W;
			case 'x':	return Rml::Input::KI_X;
			case 'y':	return Rml::Input::KI_Y;
			case 'z':	return Rml::Input::KI_Z;
			default:
				return Rml::Input::KI_UNKNOWN;
			}
		}
	}

	void RmlComponent::mouseDown(const juce::MouseEvent& _event)
	{
		Component::mouseDown(_event);
		if (!m_rmlContext)
			return;
		m_rmlContext->ProcessMouseButtonDown(toRmlMouseButton(_event), toRmlModifiers(_event));
	}

	void RmlComponent::mouseUp(const juce::MouseEvent& _event)
	{
		Component::mouseUp(_event);
		if (!m_rmlContext)
			return;
		m_rmlContext->ProcessMouseButtonUp(toRmlMouseButton(_event), toRmlModifiers(_event));
	}

	void RmlComponent::mouseMove(const juce::MouseEvent& _event)
	{
		Component::mouseMove(_event);
		if (!m_rmlContext)
			return;

		const auto pos = toRmlPosition(_event);
		m_rmlContext->ProcessMouseMove(pos.x, pos.y, toRmlModifiers(_event));
	}

	void RmlComponent::mouseDrag(const juce::MouseEvent& _event)
	{
		Component::mouseDrag(_event);
		if (!m_rmlContext)
			return;
		const auto pos = toRmlPosition(_event);
		m_rmlContext->ProcessMouseMove(pos.x, pos.y, toRmlModifiers(_event));
	}

	void RmlComponent::mouseExit(const juce::MouseEvent& _event)
	{
		Component::mouseExit(_event);
		if (m_rmlContext)
			m_rmlContext->ProcessMouseLeave();
	}

	void RmlComponent::mouseEnter(const juce::MouseEvent& _event)
	{
		Component::mouseEnter(_event);
		if (!m_rmlContext)
			return;
		const auto pos = toRmlPosition(_event);
		m_rmlContext->ProcessMouseMove(pos.x, pos.y, toRmlModifiers(_event));
	}

	void RmlComponent::mouseWheelMove(const juce::MouseEvent& _event, const juce::MouseWheelDetails& _wheel)
	{
		Component::mouseWheelMove(_event, _wheel);
		if (!m_rmlContext)
			return;
		m_rmlContext->ProcessMouseWheel(Rml::Vector2f(_wheel.deltaX, _wheel.deltaY), toRmlModifiers(_event));
	}

	void RmlComponent::mouseDoubleClick(const juce::MouseEvent& _event)
	{
		Component::mouseDoubleClick(_event);
		if (!m_rmlContext)
			return;
		m_rmlContext->ProcessMouseButtonDown(toRmlMouseButton(_event), toRmlModifiers(_event));
	}

	bool RmlComponent::keyPressed(const juce::KeyPress& _key)
	{
		if (!m_rmlContext)
			return Component::keyPressed(_key);

		const Rml::Input::KeyIdentifier key = toRmlKey(_key);

		if (key == Rml::Input::KI_UNKNOWN)
			return Component::keyPressed(_key);

		m_pressedKeys.push_back(_key);

		return m_rmlContext->ProcessKeyDown(key, toRmlModifiers(_key));
	}

	bool RmlComponent::keyStateChanged(const bool _isKeyDown)
	{
		// this API is so WTF...

		bool res = false;

		if (!_isKeyDown)
		{
			for (auto it = m_pressedKeys.begin(); it != m_pressedKeys.end();)
			{
				if (!it->isCurrentlyDown())
				{
					const auto& key = *it;
					res |= m_rmlContext->ProcessKeyUp(toRmlKey(key), toRmlModifiers(key));
					it = m_pressedKeys.erase(it);
				}
				else
				{
					++it;
				}
			}
		}
		if (res)
			return res;
		return Component::keyStateChanged(_isKeyDown);
	}

	juce::Point<int> RmlComponent::toRmlPosition(const juce::MouseEvent& _e) const
	{
		return {
			juce::roundToInt(static_cast<float>(_e.x) * m_openGLContext.getRenderingScale()), 
			juce::roundToInt(static_cast<float>(_e.y) * m_openGLContext.getRenderingScale())
		};
	}
}
