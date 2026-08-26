#include "weTablesTree.h"

#include "weTablesTreeItem.h"
#include "weData.h"
#include "xtController.h"
#include "xtEditor.h"
#include "xtWaveEditor.h"

#include "xtLib/xtMidiTypes.h"

namespace xtJucePlugin
{
	TablesTree::TablesTree(Rml::CoreInstance& _coreInstance, const std::string& _tag, WaveEditor& _editor) : Tree(_coreInstance, _tag, _editor, false)
	{
		SetClass("x-we-tablestree", true);

		for(uint16_t i=0; i<xt::wave::g_tableCount; ++i)
		{
			const xt::TableId tableId(i);

			if(xt::wave::isAlgorithmicTable(tableId) && _editor.getTableName(tableId).empty())
				continue;

			getTree().getRoot()->createChild<TablesTreeNode>(tableId);
		}

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

	Rml::ElementPtr TablesTree::createChild(const std::string& _tag)
	{
		return Rml::ElementPtr(new TablesTreeItem(GetCoreInstance(), _tag, getWaveEditor()));
	}

	void TablesTree::setSelectedEntryFromCurrentPreset() const
	{
		const auto* paramWave = getWaveParameter();

		const auto tableId = xt::TableId(static_cast<uint16_t>(paramWave->getUnnormalizedValue()));
		getWaveEditor().setSelectedTable(tableId);
	}

	void TablesTree::setSelectedTable(const xt::TableId _id)
	{
		auto& rootNode = getTree().getRoot();

		for(size_t i=0; i<rootNode->size(); ++i)
		{
			auto child = rootNode->getChild(i);

			auto* subItem = dynamic_cast<TablesTreeItem*>(child->getElement());
			if(!subItem)
				continue;
			if(subItem->getTableId() == _id)
			{
				child->setSelected(true);

				Rml::ScrollIntoViewOptions options;

				options.vertical = Rml::ScrollAlignment::Nearest;
				options.horizontal = Rml::ScrollAlignment::Nearest;
				options.behavior = Rml::ScrollBehavior::Smooth;
				options.parentage = Rml::ScrollParentage::All;

				subItem->ScrollIntoView(options);

				const auto& data = getWaveEditor().getData();

				if (const auto t = data.getTable(_id))
				{
					const auto waves = xt::State::getWavesForTable(*t);
					for (auto wave : waves)
						(void)data.sendWaveToDevice(wave);
				}

				(void)data.sendTableToDevice(_id);

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
