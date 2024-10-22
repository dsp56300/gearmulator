#include "weTablesTree.h"

#include "weTablesTreeItem.h"
#include "weData.h"
#include "xtController.h"
#include "xtEditor.h"
#include "xtWaveEditor.h"

#include "xtLib/xtMidiTypes.h"

namespace xtJucePlugin
{
	TablesTree::TablesTree(WaveEditor& _editor) : Tree(_editor)
	{
		for(uint16_t i=0; i<xt::wave::g_tableCount; ++i)
		{
			if(xt::wave::isAlgorithmicTable(xt::TableId(i)))
				continue;

			getRootItem()->addSubItem(new TablesTreeItem(_editor, xt::TableId(i)));
		}

		setIndentSize(5);

		auto* paramWave = getWaveParameter();

		m_waveParamListener.set(paramWave, [this](pluginLib::Parameter* const&)
		{
			onWaveParamChanged();
		});

		m_partChangedListener.set(getWaveEditor().getEditor().getXtController().onCurrentPartChanged, [this](const uint8_t&)
		{
			onPartChanged();
		});
	}
	
	void TablesTree::setSelectedEntryFromCurrentPreset() const
	{
		const auto* paramWave = getWaveParameter();

		const auto tableId = xt::TableId(static_cast<uint16_t>(paramWave->getUnnormalizedValue()));
		getWaveEditor().setSelectedTable(tableId);
	}

	void TablesTree::setSelectedTable(const xt::TableId _id) const
	{
		for(int i=0; i<getRootItem()->getNumSubItems(); ++i)
		{
			auto* subItem = dynamic_cast<TablesTreeItem*>(getRootItem()->getSubItem(i));
			if(!subItem)
				continue;
			if(subItem->getTableId() == _id)
			{
				subItem->setSelected(true, true, juce::dontSendNotification);
				getRootItem()->getOwnerView()->scrollToKeepItemVisible(subItem);

				auto* paramWave = getWaveParameter();
				if(paramWave->getUnnormalizedValue() != _id.rawId())
					paramWave->setUnnormalizedValueNotifyingHost(_id.rawId(), pluginLib::Parameter::Origin::Ui);

				return;
			}
		}
	}

	void TablesTree::onWaveParamChanged() const
	{
		setSelectedEntryFromCurrentPreset();
	}

	void TablesTree::onPartChanged() const
	{
		setSelectedEntryFromCurrentPreset();
	}

	pluginLib::Parameter* TablesTree::getWaveParameter() const
	{
		const auto& c = getWaveEditor().getEditor().getXtController();
		auto* param = c.getParameter("Wave", c.getCurrentPart());
		assert(param);
		return param;
	}
}
