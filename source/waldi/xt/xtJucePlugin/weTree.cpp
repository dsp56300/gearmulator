#include "weTree.h"

#include "RmlUi/Core/ElementDocument.h"

namespace xtJucePlugin
{
	namespace
	{
	const char* g_templateWithCanvas = R"(
		<treenode class="x-we-treeitem x-we-treeitem-withcanvas">
			<div id="name" class="x-we-treeitem-withcanvas-text">Name</div>
			<canvas id="graph" class="x-we-treeitem-canvas"/>
		</treenode>
	)";

	const char* g_templateWithoutCanvas = R"(
		<treenode class="x-we-treeitem x-we-treeitem-nocanvas">
			<div id="name" class="x-we-treeitem-text">Name</div>
		</treenode>
	)";

	}

	Tree::Tree(Rml::CoreInstance& _coreInstance, const std::string& _tag, WaveEditor& _editor, bool _withCanvas)
	: ElemTree(_coreInstance, _tag)
	, m_editor(_editor)
	, m_instancer([this](Rml::CoreInstance&, const std::string& _tag)
	{
		return createChild(_tag);
	})
	{
		SetClass("x-we-tree", true);

		// a template is needed
		SetInnerRML(_withCanvas ? g_templateWithCanvas : g_templateWithoutCanvas);

		setNodeInstancer(&m_instancer);
	}

	Tree::~Tree()
	{
		getTree().clear();
	}

	void Tree::paint(juce::Graphics& _g)
	{
	}
}
