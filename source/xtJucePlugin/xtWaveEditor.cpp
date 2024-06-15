#include "xtWaveEditor.h"

#include "weTree.h"
#include "weWaveTree.h"
#include "xtEditor.h"

namespace xtJucePlugin
{
	constexpr float g_scale = 2.0f * 1.3f;

	WaveEditor::WaveEditor(Component* _parent, Editor& _editor) : ComponentMovementWatcher(this), m_editor(_editor), m_data(_editor.getXtController())
	{
		setSize(static_cast<int>(static_cast<float>(_parent->getWidth()) / g_scale), static_cast<int>(static_cast<float>(_parent->getHeight()) / g_scale));
		setTransform(juce::AffineTransform::scale(g_scale));
		_parent->addAndMakeVisible(this);

		addComponentListener(this);

		m_waveTree.reset(new WaveTree(*this));
		m_waveTree->setSize(550, getHeight());
		addAndMakeVisible(m_waveTree.get());
	}

	WaveEditor::~WaveEditor()
	{
		m_waveTree.reset();

		removeComponentListener(this);
	}

	void WaveEditor::visibilityChanged()
	{
		Component::visibilityChanged();
		checkFirstTimeVisible();
	}

	void WaveEditor::parentHierarchyChanged()
	{
		Component::parentHierarchyChanged();
		checkFirstTimeVisible();
	}

	void WaveEditor::onReceiveWave(const pluginLib::MidiPacket::Data& _data, const std::vector<uint8_t>& _msg)
	{
		m_data.onReceiveWave(_data, _msg);
	}

	void WaveEditor::componentVisibilityChanged()
	{
		checkFirstTimeVisible();
	}

	void WaveEditor::checkFirstTimeVisible()
	{
		if(isShowing() && !m_wasVisible)
		{
			m_wasVisible = true;
			onFirstTimeVisible();
		}
	}

	void WaveEditor::onFirstTimeVisible()
	{
		m_data.requestData();
	}
}
