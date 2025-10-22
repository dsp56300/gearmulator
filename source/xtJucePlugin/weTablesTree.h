#pragma once

#include "weTree.h"

#include "xtLib/xtId.h"

#include "jucePluginLib/parameterlistener.h"

namespace xtJucePlugin
{
	class TablesTree : public Tree
	{
	public:
		explicit TablesTree(Rml::CoreInstance& _coreInstance, const std::string& _tag, WaveEditor& _editor);

		Rml::ElementPtr createChild(const std::string& _tag) override;

		void setSelectedTable(xt::TableId _id);

		void setSelectedEntryFromCurrentPreset() const;

	private:
		void onWaveParamChanged() const;
		void onPartChanged() const;
		pluginLib::Parameter* getWaveParameter() const;

		pluginLib::ParameterListener m_waveParamListener;
		baseLib::EventListener<uint8_t> m_partChangedListener;
	};
}
