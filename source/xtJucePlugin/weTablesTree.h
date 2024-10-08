#pragma once

#include "weTree.h"

#include "xtLib/xtId.h"

#include "jucePluginLib/parameterlistener.h"

namespace xtJucePlugin
{
	class TablesTree : public Tree
	{
	public:
		explicit TablesTree(WaveEditor& _editor);

		void setSelectedTable(xt::TableId _id) const;

		void setSelectedEntryFromCurrentPreset() const;

	private:
		void onWaveParamChanged() const;
		void onPartChanged() const;
		pluginLib::Parameter* getWaveParameter() const;

		pluginLib::ParameterListener m_waveParamListener;
		pluginLib::EventListener<uint8_t> m_partChangedListener;
	};
}
