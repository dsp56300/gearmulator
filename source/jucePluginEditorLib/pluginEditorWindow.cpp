#include "pluginEditorWindow.h"
#include "pluginEditorState.h"

#include "patchmanager/patchmanager.h"

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
			setGuiScale(getChildComponent(0), static_cast<float>(_scale));
	};

	m_state.enableBindings();

	setUiRoot(m_state.getUiRoot());
}

EditorWindow::~EditorWindow()
{
	m_state.evSetGuiScale = [&](int){};
	m_state.evSkinLoaded = [&](juce::Component*){};

	m_state.disableBindings();

	setUiRoot(nullptr);
}

void EditorWindow::resized()
{
	AudioProcessorEditor::resized();

	auto* comp = getChildComponent(0);
	if(!comp)
		return;

	if(!m_state.getWidth() || !m_state.getHeight())
		return;

	const auto targetW = getWidth();
	const auto targetH = getHeight();

	const auto scaleX = static_cast<float>(getWidth()) / static_cast<float>(m_state.getWidth());
	const auto scaleY = static_cast<float>(getHeight()) / static_cast<float>(m_state.getHeight());

	const auto scale = std::min(scaleX, scaleY);

	const auto w = scale * static_cast<float>(m_state.getWidth());
	const auto h = scale * static_cast<float>(m_state.getHeight());

	const auto offX = (static_cast<float>(targetW) - w) * 0.5f;
	const auto offY = (static_cast<float>(targetH) - h) * 0.5f;

	comp->setTransform(juce::AffineTransform::scale(scale,scale).translated(offX, offY));

	const auto percent = 100.f * scale / m_state.getRootScale();
	m_config.setValue("scale", percent);
	m_config.saveIfNeeded();

	AudioProcessorEditor::resized();
}

void EditorWindow::setGuiScale(juce::Component* _comp, const float _percent)
{
	if(!m_state.getWidth() || !m_state.getHeight())
		return;

	const auto s = _percent / 100.0f * m_state.getRootScale();
	_comp->setTransform(juce::AffineTransform::scale(s,s));

	const auto w = static_cast<int>(static_cast<float>(m_state.getWidth()) * s);
	const auto h = static_cast<int>(static_cast<float>(m_state.getHeight()) * s);

	setSize(w, h);

	m_config.setValue("scale", _percent);
	m_config.saveIfNeeded();
}

void EditorWindow::setUiRoot(juce::Component* _component)
{
	removeAllChildren();

	if(!_component)
		return;

	if(!m_state.getWidth() || !m_state.getHeight())
		return;

    const auto scale = static_cast<float>(m_config.getDoubleValue("scale", 100));

	addAndMakeVisible(_component);
	setGuiScale(_component, scale);

	m_sizeConstrainer.setMinimumSize(m_state.getWidth() / 10, m_state.getHeight() / 10);
	m_sizeConstrainer.setMaximumSize(m_state.getWidth() * 4, m_state.getHeight() * 4);

	m_sizeConstrainer.setFixedAspectRatio(static_cast<double>(m_state.getWidth()) / static_cast<double>(m_state.getHeight()));
	
	setResizable(true, true);
	setConstrainer(nullptr);
	setConstrainer(&m_sizeConstrainer);
}

void EditorWindow::mouseDown(const juce::MouseEvent& event)
{
	if(!event.mods.isPopupMenu())
	{
		AudioProcessorEditor::mouseDown(event);
		return;
	}

	if(event.eventComponent)
	{
		// file browsers have their own menu, do not display two menus at once
		if(event.eventComponent->findParentComponentOfClass<juce::FileBrowserComponent>())
			return;

		// patch manager has its own context menu, too
		if (event.eventComponent->findParentComponentOfClass<patchManager::PatchManager>())
			return;
	}

	if(dynamic_cast<juce::TextEditor*>(event.eventComponent))
		return;
	if(dynamic_cast<juce::Button*>(event.eventComponent))
		return;
	if(event.eventComponent->findParentComponentOfClass<juce::TreeView>())
		return;

	m_state.openMenu(&event);
}

}