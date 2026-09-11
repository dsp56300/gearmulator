#pragma once

#include "Emu88Editor.h"
#include "juceRmlUi/rmlDragData.h"
#include "juceRmlUi/rmlDragSource.h"
#include "juceRmlUi/rmlDragTarget.h"
#include <algorithm>

namespace emu88Player
{
	class PlaylistDragData final : public juceRmlUi::DragData
	{
	public:
		explicit PlaylistDragData(const size_t _index) : index(_index) {}
		size_t index;
	};

	class PlaylistRowDrag final : public juceRmlUi::DragSource, public juceRmlUi::DragTarget
	{
	public:
		using MoveCallback = std::function<void(size_t, size_t)>;
		using FilesCallback = std::function<void(const std::vector<std::string>&)>;

		PlaylistRowDrag(Rml::Element* _element, const size_t _index,
		                MoveCallback _move, FilesCallback _files)
			: DragSource(_element), DragTarget(_element), m_index(_index),
			  m_move(std::move(_move)), m_files(std::move(_files))
		{
			_element->SetProperty(Rml::PropertyId::Drag, Rml::Style::Drag::Clone);
			setAllowLocations(false, true);
		}

		std::unique_ptr<juceRmlUi::DragData> createDragData() override
		{
			return std::make_unique<PlaylistDragData>(m_index);
		}

		bool canDrop(const Rml::Event& _event, const juceRmlUi::DragSource* _source) override
		{
			if(dynamic_cast<const PlaylistDragData*>(_source ? _source->getDragData() : nullptr))
				return true;
			return DragTarget::canDrop(_event, _source);
		}

		bool canDropFiles(const Rml::Event&, const std::vector<std::string>& _files) override
		{
			return std::any_of(_files.begin(), _files.end(), [](const std::string& _file)
			{
				const auto extension = juce::File(_file).getFileExtension().toLowerCase();
				return extension == ".mid" || extension == ".midi" || extension == ".rcp" || extension == ".r36";
			});
		}

		void drop(const Rml::Event&, const juceRmlUi::DragSource*,
		          const juceRmlUi::DragData* _data) override
		{
			const auto* data = dynamic_cast<const PlaylistDragData*>(_data);
			if(!data)
				return;
			const auto insertion = m_index +
				(getDragLocationV() == DragLocation::Bottom ? size_t{1} : size_t{0});
			m_move(data->index, insertion);
		}

		void dropFiles(const Rml::Event& _event, const juceRmlUi::FileDragData*,
		               const std::vector<std::string>& _files) override
		{
			// Rows are nested inside the playlist-wide target. Dragdrop bubbles, so
			// only the target it was dispatched to may consume this OS file drop.
			if(_event.GetTargetElement() != _event.GetCurrentElement())
				return;
			m_files(_files);
		}

	private:
		size_t m_index;
		MoveCallback m_move;
		FilesCallback m_files;
	};

	class PlaylistDropTarget final : public juceRmlUi::DragTarget
	{
	public:
		using MoveCallback = std::function<void(size_t, size_t)>;
		using FilesCallback = std::function<void(const std::vector<std::string>&)>;

		PlaylistDropTarget(Rml::Element* _element, MoveCallback _move, FilesCallback _files,
		                   std::function<size_t()> _size)
			: DragTarget(_element), m_move(std::move(_move)), m_files(std::move(_files)),
			  m_size(std::move(_size))
		{
			setAllowLocations(false, false);
		}

		bool canDropFiles(const Rml::Event&, const std::vector<std::string>& _files) override
		{
			return std::any_of(_files.begin(), _files.end(), [](const std::string& _file)
			{
				const auto extension = juce::File(_file).getFileExtension().toLowerCase();
				return extension == ".mid" || extension == ".midi" || extension == ".rcp" || extension == ".r36";
			});
		}

		void drop(const Rml::Event&, const juceRmlUi::DragSource*,
		          const juceRmlUi::DragData* _data) override
		{
			if(const auto* data = dynamic_cast<const PlaylistDragData*>(_data))
				m_move(data->index, m_size());
		}

		void dropFiles(const Rml::Event& _event, const juceRmlUi::FileDragData*,
		               const std::vector<std::string>& _files) override
		{
			if(_event.GetTargetElement() != _event.GetCurrentElement())
				return;
			m_files(_files);
		}

	private:
		MoveCallback m_move;
		FilesCallback m_files;
		std::function<size_t()> m_size;
	};
}
