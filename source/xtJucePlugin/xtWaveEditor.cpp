#include "xtWaveEditor.h"

#include "weTree.h"
#include "weWaveTree.h"
#include "xtEditor.h"

namespace xtJucePlugin
{
	WaveEditor::WaveEditor(Editor& _editor) : ComponentMovementWatcher(this), m_editor(_editor), m_data(_editor.getXtController())
	{
		addComponentListener(this);
	}

	WaveEditor::~WaveEditor()
	{
		m_waveTree.reset();

		removeComponentListener(this);
	}

	void WaveEditor::initialize()
	{
		auto* waveListParent = m_editor.findComponent("wecWaveList");
		m_waveTree.reset(new WaveTree(*this));
		waveListParent->addAndMakeVisible(m_waveTree.get());
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

	void WaveEditor::onReceiveWave(const pluginLib::MidiPacket::Data& _data, const std::vector<uint8_t>& _msg)
	{
		m_data.onReceiveWave(_data, _msg);
	}
}
