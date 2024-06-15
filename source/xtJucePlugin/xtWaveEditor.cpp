#include "xtWaveEditor.h"

#include "weTree.h"
#include "weWaveTree.h"
#include "weTablesTree.h"
#include "weControlTree.h"
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
		m_controlTree.reset();
		m_tablesTree.reset();

		removeComponentListener(this);
	}

	void WaveEditor::initialize()
	{
		auto* waveListParent = m_editor.findComponent("wecWaveList");
		auto* tablesListParent = m_editor.findComponent("wecWavetableList");
		auto* controlListParent = m_editor.findComponent("wecWaveControlTable");

		m_waveTree.reset(new WaveTree(*this));
		m_tablesTree.reset(new TablesTree(*this));
		m_controlTree.reset(new ControlTree(*this));

		waveListParent->addAndMakeVisible(m_waveTree.get());
		tablesListParent->addAndMakeVisible(m_tablesTree.get());
		controlListParent->addAndMakeVisible(m_controlTree.get());
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

	void WaveEditor::onReceiveTable(const pluginLib::MidiPacket::Data& _data, const std::vector<uint8_t>& _msg)
	{
		m_data.onReceiveTable(_data, _msg);
	}

	void WaveEditor::setSelectedTable(uint32_t _index)
	{
		if(m_selectedTable == _index)
			return;

		m_selectedTable = _index;
		m_controlTree->setTable(_index);
	}
}
