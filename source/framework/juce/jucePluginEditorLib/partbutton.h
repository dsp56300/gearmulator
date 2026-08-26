#pragma once

#include <cstdint>

#include "juceRmlUi/rmlDragSource.h"
#include "juceRmlUi/rmlDragTarget.h"

namespace Rml
{
	class Element;
}

namespace jucePluginEditorLib
{
	namespace patchManager
	{
		class ListModel;
	}

	class Editor;

	class PartButton : juceRmlUi::DragSource, juceRmlUi::DragTarget
	{
	public:
		explicit PartButton(Rml::Element* _button, Editor& _editor);

		void initalize(const uint8_t _part);

		auto getPart() const
		{
			return m_part;
		}

		bool canDrop(const Rml::Event& _event, const DragSource* _source) override;
		bool canDropFiles(const Rml::Event& _event, const std::vector<std::string>& _files) override;

		void drop(const Rml::Event& _event, const DragSource* _source, const juceRmlUi::DragData* _data) override;
		void dropFiles(const Rml::Event& _event, const juceRmlUi::FileDragData* _data, const std::vector<std::string>& _files) override;

		std::unique_ptr<juceRmlUi::DragData> createDragData() override;

		virtual void onClick(Rml::Event&) {}

		void setVisible(bool _visible) const;

		Rml::Element* getElement() const { return m_button; }

		void setChecked(bool _checked) const;

	private:
		Rml::Element* m_button = nullptr;
		Editor& m_editor;
		uint8_t m_part = 0xff;
		bool m_isDragTarget = false;
	};
}
