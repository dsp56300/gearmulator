#pragma once

#include <cstdint>

#include "../juceUiLib/button.h"

namespace jucePluginEditorLib
{
	namespace patchManager
	{
		class List;
	}

	class Editor;

	template<typename T>
	class PartButton : public genericUI::Button<T>, public juce::DragAndDropTarget
	{
	public:
		template<class... TArgs>
		explicit PartButton(Editor& _editor, const TArgs&... _args) : genericUI::Button<T>(_args...) , m_editor(_editor)
		{
		}

		void initalize(const uint8_t _part)
		{
			m_part = _part;
		}

		auto getPart() const
		{
			return m_part;
		}

		bool isInterestedInDragSource(const SourceDetails& _dragSourceDetails) override;

		void itemDragEnter(const SourceDetails& dragSourceDetails) override;
		void itemDragExit(const SourceDetails& _dragSourceDetails) override;
		void itemDropped(const SourceDetails& _dragSourceDetails) override;

		void paint(juce::Graphics& g) override;

		void mouseDrag(const juce::MouseEvent& _event) override;
		void mouseUp(const juce::MouseEvent& _event) override;

		virtual void onClick() {}

	private:
		void setIsDragTarget(const bool _isDragTarget);


		Editor& m_editor;
		uint8_t m_part = 0xff;
		bool m_isDragTarget = false;
	};
}
