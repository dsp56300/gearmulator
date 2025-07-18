#include "juceRmlDrag.h"

#include "juceRmlComponent.h"
#include "rmlDragData.h"
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/ElementDocument.h"

namespace juceRmlUi
{
	RmlDrag::FileDragSource::FileDragSource(Rml::Element* _element, const juce::StringArray& _files) : DragSource(_element)
	{
		m_files.reserve(_files.size());

		for (const auto& file : _files)
			m_files.emplace_back(file.toStdString());
	}

	std::unique_ptr<DragData> RmlDrag::FileDragSource::createDragData()
	{
		auto data = std::make_unique<FileDragData>();
		data->files = m_files;
		return data;
	}

	RmlDrag::RmlDrag(RmlComponent& _component) : m_component(_component)
	{
	}

	RmlDrag::~RmlDrag()
	= default;

	// ReSharper disable once CppMemberFunctionMayBeStatic
	bool RmlDrag::isInterestedInFileDrag(const juce::StringArray& _files)
	{
		// say yes to everything, individual elements need to decide if they are interested in the drag source
		return true;
	}

	void RmlDrag::fileDragEnter(const juce::StringArray& _files, int _x, int _y)
	{
		startDrag(_x, _y, _files);
	}

	void RmlDrag::fileDragMove(const juce::StringArray& _files, int _x, int _y) const
	{
		if (!m_draggable)
			return;
		const auto pos = m_component.toRmlPosition(_x, _y);
		m_component.getContext()->ProcessMouseMove(pos.x, pos.y, 0);
	}

	void RmlDrag::fileDragExit(const juce::StringArray&)
	{
		stopDrag();
		m_component.getContext()->ProcessMouseLeave();
	}

	void RmlDrag::filesDropped(const juce::StringArray& _files, int _x, int _y)
	{
		if (!m_draggable)
			return;
		m_component.getContext()->ProcessMouseButtonUp(0,0);
		stopDrag();
	}

	// ReSharper disable once CppMemberFunctionMayBeStatic
	bool RmlDrag::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails)
	{
		// say yes to everything, individual elements need to decide if they are interested in the drag source
		return true;
	}

	void RmlDrag::itemDragEnter(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails)
	{
	}

	void RmlDrag::itemDragExit(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails)
	{
	}

	void RmlDrag::itemDropped(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails)
	{
	}

	void RmlDrag::startDrag(int _x, int _y, const juce::StringArray& _files)
	{
		if (m_draggable)
			return;

		//  create a fake element that we can drag around. To start dragging we need to inject a mouse down and move event into the context.

		const auto pos = m_component.toRmlPosition(_x, _y);

		constexpr auto size = 100.0f;

		Rml::ElementPtr draggable = m_component.getDocument()->CreateElement("div");
		m_dragSource.reset(new FileDragSource(draggable.get(), _files));

		draggable->SetProperty(Rml::PropertyId::Position, Rml::Property(Rml::Style::Position::Absolute));

		draggable->SetProperty(Rml::PropertyId::Left, Rml::Property(static_cast<float>(pos.x) - size/2.0f, Rml::Unit::PX));
		draggable->SetProperty(Rml::PropertyId::Top, Rml::Property(static_cast<float>(pos.y) - size/2.0f, Rml::Unit::PX));
		draggable->SetProperty(Rml::PropertyId::Width, Rml::Property(size, Rml::Unit::PX));
		draggable->SetProperty(Rml::PropertyId::Height, Rml::Property(size, Rml::Unit::PX));

		draggable->SetProperty(Rml::PropertyId::ZIndex, Rml::Property(1000, Rml::Unit::NUMBER));

		draggable->SetProperty(Rml::PropertyId::BackgroundColor, Rml::Property(Rml::Colourb(255, 0, 255, 255), Rml::Unit::COLOUR));

		draggable->SetProperty(Rml::PropertyId::Drag, Rml::Style::Drag::DragDrop);

		m_draggable = m_component.getDocument()->AppendChild(std::move(draggable));

		// force layout rebuild
		m_component.getContext()->Update();

		// force start dragging
		m_component.getContext()->ProcessMouseMove(pos.x, pos.y, 0);
		m_component.getContext()->ProcessMouseButtonDown(0,0);
		m_component.getContext()->ProcessMouseMove(pos.x + 1, pos.y - 1, 0);
		m_component.getContext()->ProcessMouseMove(pos.x - 1, pos.y + 1, 0);
		m_component.getContext()->ProcessMouseMove(pos.x, pos.y, 0);
	}

	void RmlDrag::stopDrag()
	{
		if (!m_draggable)
			return;
		m_draggable->GetParentNode()->RemoveChild(m_draggable);
		m_draggable = nullptr;
	}
}
