#include "PatchBrowser.h"

#include "VirusEditor.h"

namespace genericVirusUI
{
	PatchBrowser::PatchBrowser(const VirusEditor& _editor) : m_patchBrowser(_editor.getParameterBinding(), _editor.getController())
	{
		// We use the old patch browser for now. Fit into the desired parent
		auto* pagePresets = _editor.findComponent("ContainerPresetBrowser");

		const auto parentBounds = pagePresets->getBounds();

		const auto w = parentBounds.getWidth() >> 1;
		const auto h = parentBounds.getHeight() >> 1;

		m_patchBrowser.setBounds(0,0,w,h);

		const auto searchY = h - m_patchBrowser.getSearchBox().getHeight() - 5;

		m_patchBrowser.getBankList().setBounds(0, 0, w>>1, h);
		m_patchBrowser.getPatchList().setBounds(w>>1, 0, w>>1, searchY);
		m_patchBrowser.getSearchBox().setBounds(w>>1, searchY, w>>1, m_patchBrowser.getSearchBox().getHeight());

		pagePresets->addAndMakeVisible(&m_patchBrowser);

		m_patchBrowser.setTransform(juce::AffineTransform::scale(2.0f));
	}
}
