#include "weTablesTree.h"

#include "weTablesTreeItem.h"
#include "weTypes.h"
#include "weData.h"

#include "xtLib/xtMidiTypes.h"

namespace xtJucePlugin
{
	TablesTree::TablesTree(WaveEditor& _editor): Tree(_editor)
	{
		for(uint32_t i=0; i<xt::Wave::g_tableCount; ++i)
		{
			if(WaveEditorData::isAlgorithmicTable(i))
				continue;

			getRootItem()->addSubItem(new TablesTreeItem(_editor, i));
		}
		setIndentSize(5);
	}
}
