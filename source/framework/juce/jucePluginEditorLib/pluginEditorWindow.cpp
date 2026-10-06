#include "pluginEditorWindow.h"

#include "pluginEditor.h"
#include "pluginEditorState.h"

#include "dsp56kBase/logging.h"

#include "juceRmlPlugin/rmlParameterBinding.h"

#include "juceRmlUi/juceRmlComponent.h"

#include "RmlUi/Core/Elements/ElementFormControlInput.h"

namespace jucePluginEditorLib
{

//==============================================================================
EditorWindow::EditorWindow(juce::AudioProcessor& _p, PluginEditorState& _s, juce::PropertiesFile& _config)
	: AudioProcessorEditor(&_p), m_state(_s), m_config(_config)
{
	addMouseListener(this, true);

	m_state.evSkinLoaded = [&](juce::Component* _component)
	{
		setUiRoot(_component);
	};

	m_state.evSetGuiScale = [&](const int _scale)
	{
		if(getNumChildComponents() > 0)
			setGuiScale(static_cast<float>(_scale));
	};

	m_state.evSkinSizeChanged = [&]
	{
		if(getNumChildComponents() > 0)
			onSkinSizeChanged();
	};

	m_state.evFreeWindowSizeRestored = [&]
	{
		if(getNumChildComponents() > 0 && isFreeWindowMode())
			setSizeIfDifferent(constrainSize({m_state.getFreeWindowSize().width, m_state.getFreeWindowSize().height}));
	};

	setUiRoot(m_state.getUiRoot());
}

EditorWindow::~EditorWindow()
{
	m_state.evSetGuiScale = [&](int){};
	m_state.evSkinSizeChanged = [&]{};
	m_state.evFreeWindowSizeRestored = [&]{};
	m_state.evSkinLoaded = [&](juce::Component*){};

	setUiRoot(nullptr);
}

void EditorWindow::resized()
{
	AudioProcessorEditor::resized();

	if(!m_state.getWidth() || !m_state.getHeight())
		return;

	const auto w = getWidth();
	const auto h = getHeight();

	// in the free window mode a new size is more or less room, the scale stays
	if (isFreeWindowMode())
	{
		if (!m_state.resizeEditor(w,h))
			return;
		// same reason as below for not remembering the minimum
		if (!isMinimumSize(w, h))
			m_state.setFreeWindowSize({w, h});
		startTimer(1);
		return;
	}

	const auto scaleX = static_cast<float>(w) / static_cast<float>(m_state.getWidth());
	const auto scaleY = static_cast<float>(h) / static_cast<float>(m_state.getHeight());

	// A size that the current scale produces keeps that scale. Taking it from the pixels again would round it, and a
	// skin that changes its own size would then drift by a pixel each time. Any other size is the user's or the host's.
	const auto expected = getSizeForSkinScale(m_skinScale);
	if (std::abs(expected.x - w) > 1 || std::abs(expected.y - h) > 1)
		m_skinScale = std::min(scaleX, scaleY);

	if (!m_state.resizeEditor(w,h))
		return;

	// Any size that is too small ends up at the minimum, and hosts, window managers and display changes ask for those
	// without the user doing anything. A size at the minimum therefore says nothing about the scale the user wants,
	// and remembering it opened every later window at the minimum.
	if (!isMinimumSize(w, h))
	{
		const auto percent = 100.f * m_skinScale / m_state.getRootScale();
		m_config.setValue("scale", percent);
		m_config.saveIfNeeded();
	}

	// Prettymuch unbelievable Juce VST3 bug, but our root component is a child of the VST3 editor component
	// and that one is not resized! The host window is, the first child (our editor component) is, but the
	// root component is not! This is no drama as long as you do not have a juce OpenGL context, because
	// that one uses the "top level component" to set the clipping rectangle! W T F
	startTimer(1);
}

int EditorWindow::getControlParameterIndex(Component& _component)
{
	// This code relies on the fact that getComponentAt() is called with a XY position
	// first and then the parameter is queried for that returned component afterwards.
	// As we do not have Juce components, we remember the last Rml element that was
	// under the mouse and query the parameter binding for that element here.
	// It would be better if there was a function like "getParameterForPosition" but unfortunately
	// Juce does not provide that.
	if (const auto* editor = m_state.getEditor())
	{
		if (const auto* comp = editor->getRmlComponent())
		{
			if (const auto* binding = editor->getRmlParameterBinding())
			{
				if (const auto* elem = comp->getLastElementByGetComponentAt())
				{
					if (const auto* param = binding->getParameterForElement(elem))
						return param->getParameterIndex();

					if (const auto* parent = elem->GetParentNode())
					{
						if (dynamic_cast<const Rml::ElementFormControlInput*>(parent))
						{
							if (const auto* param = binding->getParameterForElement(parent))
								return param->getParameterIndex();
						}
					}
				}
			}
		}
	}

	return AudioProcessorEditor::getControlParameterIndex(_component);
}

void EditorWindow::setGuiScale(const float _percent)
{
	if(!m_state.getWidth() || !m_state.getHeight())
		return;

	m_skinScale = _percent / 100.0f * m_state.getRootScale();

	if (isFreeWindowMode())
	{
		// zoom: the window keeps its size, the skin gets less or more room in it
		getRmlComponent()->setFreeWindowScale(m_skinScale);
		updateSizeConstrainer();
		setSizeIfDifferent(constrainSize({getWidth(), getHeight()}));
	}
	else
	{
		const auto size = getSizeForSkinScale(m_skinScale);
		setSize(size.x, size.y);
	}

	m_config.setValue("scale", _percent);
	m_config.saveIfNeeded();
}

juce::Point<int> EditorWindow::getSizeForScale(const float _percent) const
{
	return getSizeForSkinScale(_percent / 100.0f * m_state.getRootScale());
}

juce::Point<int> EditorWindow::getSizeForSkinScale(const float _scale) const
{
	return { static_cast<int>(static_cast<float>(m_state.getWidth()) * _scale),
		static_cast<int>(static_cast<float>(m_state.getHeight()) * _scale) };
}

bool EditorWindow::isMinimumSize(const int _width, const int _height) const
{
	return _width <= m_sizeConstrainer.getMinimumWidth() || _height <= m_sizeConstrainer.getMinimumHeight();
}

void EditorWindow::setUiRoot(juce::Component* _component)
{
	removeAllChildren();
	setConstrainer(nullptr);

	if(!_component)
		return;

	if(!m_state.getWidth() || !m_state.getHeight())
		return;

	// versions before this one did save the minimum, and a config that holds it would keep every window at the minimum
	auto scale = static_cast<float>(m_config.getDoubleValue("scale", 100));

	if (isFreeWindowMode())
	{
		// the minimum of the fixed window mode, the one such a config holds; the skin's own minimum can be its default
		if (const auto s = getSizeForScale(scale); s.x <= m_state.getWidth() / 10 || s.y <= m_state.getHeight() / 10)
			scale = 100.0f;
		m_skinScale = scale / 100.0f * m_state.getRootScale();

		getRmlComponent()->setFreeWindowScale(m_skinScale);
		updateSizeConstrainer();

		const auto& saved = m_state.getFreeWindowSize();
		const auto size = constrainSize(saved.isValid() ? juce::Point<int>(saved.width, saved.height) : getSizeForSkinScale(m_skinScale));
		setSize(size.x, size.y);
	}
	else
	{
		updateSizeConstrainer();

		const auto size = getSizeForScale(scale);
		if (isMinimumSize(size.x, size.y))
			scale = 100.0f;
		setGuiScale(scale);
	}

	_component->setSize(getWidth(), getHeight());

	addAndMakeVisible(_component);

	setResizable(true, true);
	setConstrainer(&m_sizeConstrainer);
}

void EditorWindow::updateSizeConstrainer()
{
	if (!isFreeWindowMode())
	{
		m_sizeConstrainer.setMinimumSize(m_state.getWidth() / 10, m_state.getHeight() / 10);
		m_sizeConstrainer.setMaximumSize(m_state.getWidth() * 4, m_state.getHeight() * 4);

		m_sizeConstrainer.setFixedAspectRatio(static_cast<double>(m_state.getWidth()) / static_cast<double>(m_state.getHeight()));
		return;
	}

	// The limits come from min-width, min-height, max-width and max-height of the body, at the current zoom. Without
	// a minimum it is the one of the fixed window mode, a minimum that hosts and window managers ask for.
	const auto* rml = getRmlComponent();
	const auto minDoc = rml->getMinimumDocumentSize();
	const auto maxDoc = rml->getMaximumDocumentSize();

	constexpr float noMax = 0x3fffffff;

	const auto minW = minDoc.x > 0 ? juce::roundToInt(minDoc.x * m_skinScale) : m_state.getWidth() / 10;
	const auto minH = minDoc.y > 0 ? juce::roundToInt(minDoc.y * m_skinScale) : m_state.getHeight() / 10;
	const auto maxW = juce::roundToInt(std::min(maxDoc.x * m_skinScale, noMax));
	const auto maxH = juce::roundToInt(std::min(maxDoc.y * m_skinScale, noMax));

	m_sizeConstrainer.setFixedAspectRatio(0.0);
	m_sizeConstrainer.setMinimumSize(minW, minH);
	m_sizeConstrainer.setMaximumSize(std::max(minW, maxW), std::max(minH, maxH));
}

void EditorWindow::onSkinSizeChanged()
{
	updateSizeConstrainer();

	// the skin has new room, from the window or the zoom, or new limits that the window has to respect
	if (isFreeWindowMode())
	{
		setSizeIfDifferent(constrainSize({getWidth(), getHeight()}));
		return;
	}

	// the skin changed its own size, to fold part of itself away for example, which keeps the scale
	const auto size = getSizeForSkinScale(m_skinScale);

	setSize(size.x, size.y);
}

juceRmlUi::RmlComponent* EditorWindow::getRmlComponent() const
{
	const auto* editor = m_state.getEditor();
	return editor ? editor->getRmlComponent() : nullptr;
}

bool EditorWindow::isFreeWindowMode() const
{
	const auto* rml = getRmlComponent();
	return rml && rml->isFreeWindowMode();
}

juce::Point<int> EditorWindow::constrainSize(const juce::Point<int>& _size) const
{
	return { juce::jlimit(m_sizeConstrainer.getMinimumWidth(), m_sizeConstrainer.getMaximumWidth(), _size.x),
		juce::jlimit(m_sizeConstrainer.getMinimumHeight(), m_sizeConstrainer.getMaximumHeight(), _size.y) };
}

void EditorWindow::setSizeIfDifferent(const juce::Point<int>& _size)
{
	if (_size.x != getWidth() || _size.y != getHeight())
		setSize(_size.x, _size.y);
}

void EditorWindow::timerCallback()
{
	fixParentWindowSize();
	stopTimer();
}

void EditorWindow::fixParentWindowSize() const
{
	const auto w = getWidth();
	const auto h = getHeight();

	auto* parent = getParentComponent();

	while (parent)
	{
		if (parent->getWidth() < w || parent->getHeight() < h)
		{
			LOG("Parent " << parent->getName() << " has wrong size: " << parent->getName() <<
				", expected: " << w << "x" << h <<
				", actual: " << parent->getWidth() << "x" << parent->getHeight());
			parent->setSize(w, h);
		}

		parent = parent->getParentComponent();
	}
}
}
