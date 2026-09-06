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

	setUiRoot(m_state.getUiRoot());
}

EditorWindow::~EditorWindow()
{
	m_state.evSetGuiScale = [&](int){};
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

	const auto scaleX = static_cast<float>(w) / static_cast<float>(m_state.getWidth());
	const auto scaleY = static_cast<float>(h) / static_cast<float>(m_state.getHeight());

	const auto scale = std::min(scaleX, scaleY);

	if (!m_state.resizeEditor(w,h))
		return;

	const auto percent = 100.f * scale / m_state.getRootScale();
	m_config.setValue("scale", percent);
	m_config.saveIfNeeded();

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

	const auto s = _percent / 100.0f * m_state.getRootScale();

	const auto w = static_cast<int>(static_cast<float>(m_state.getWidth()) * s);
	const auto h = static_cast<int>(static_cast<float>(m_state.getHeight()) * s);

	setSize(w, h);

	m_config.setValue("scale", _percent);
	m_config.saveIfNeeded();
}

void EditorWindow::setUiRoot(juce::Component* _component)
{
	removeAllChildren();
	setConstrainer(nullptr);

	if(!_component)
		return;

	if(!m_state.getWidth() || !m_state.getHeight())
		return;

	m_sizeConstrainer.setMinimumSize(m_state.getWidth() / 10, m_state.getHeight() / 10);
	m_sizeConstrainer.setMaximumSize(m_state.getWidth() * 4, m_state.getHeight() * 4);

	m_sizeConstrainer.setFixedAspectRatio(static_cast<double>(m_state.getWidth()) / static_cast<double>(m_state.getHeight()));
	
    const auto scale = static_cast<float>(m_config.getDoubleValue("scale", 100));
	setGuiScale(scale);

	_component->setSize(getWidth(), getHeight());

	addAndMakeVisible(_component);

	setResizable(true, true);
	setConstrainer(&m_sizeConstrainer);
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
