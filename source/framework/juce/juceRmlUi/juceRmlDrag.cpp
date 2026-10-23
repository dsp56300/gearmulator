#include "juceRmlDrag.h"

#include "juceRmlComponent.h"

#include "rmlDragData.h"
#include "rmlHelper.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/ElementDocument.h"

namespace juceRmlUi
{
	struct JuceDraggable : juce::ReferenceCountedObject
	{
		DragData* data = nullptr;
	};

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

	void RmlDrag::onDocumentLoaded()
	{
		auto* doc = m_component.getDocument();

		doc->AddEventListener(Rml::EventId::Dragstart, this);
		doc->AddEventListener(Rml::EventId::Dragend, this);
	}

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
		m_lastDropHandled = false;

		// No draggable means the drag never started inside the document, so no element can have
		// taken it - the files still have to go somewhere, which is the whole point below.
		if (m_draggable)
		{
			// This is what makes RmlUi deliver the drop: the fake draggable is released over
			// whatever is under the cursor, and a drag target there - a wave slot, a patch list -
			// takes it.
			m_component.getContext()->ProcessMouseButtonUp(0,0);

			m_lastDropHandled = m_juceDragSource && m_juceDragSource->isHandled();

			stopDrag();
		}

		if (m_lastDropHandled || !m_onUnhandledFiles)
			return;

		std::vector<std::string> files;
		files.reserve(_files.size());

		for (const auto& file : _files)
			files.emplace_back(file.toStdString());

		m_onUnhandledFiles(files);
	}

	// ReSharper disable once CppMemberFunctionMayBeStatic
	bool RmlDrag::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails)
	{
		// say yes to everything, individual elements need to decide if they are interested in the drag source
		return true;
	}

	void RmlDrag::itemDragEnter(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails)
	{
		// not a drag of this component, which stays one of elements
		const auto* draggable = dynamic_cast<JuceDraggable*>(_dragSourceDetails.description.getObject());
		if (!draggable || draggable == m_juceDraggable || !draggable->data || !draggable->data->canExportAsFiles())
			return;

		std::vector<std::string> files;
		bool temporary = false;
		draggable->data->getFilesForExport(files, temporary);
		m_foreignFiles.clear();
		for (const auto& file : files)
			m_foreignFiles.add(juce::String(file));

		if (!m_foreignFiles.isEmpty())
			fileDragEnter(m_foreignFiles, _dragSourceDetails.localPosition.x, _dragSourceDetails.localPosition.y);
	}

	void RmlDrag::itemDragMove(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails) const
	{
		if (!m_foreignFiles.isEmpty())
			fileDragMove(m_foreignFiles, _dragSourceDetails.localPosition.x, _dragSourceDetails.localPosition.y);
	}

	void RmlDrag::itemDragExit(const juce::DragAndDropTarget::SourceDetails&)
	{
		if (m_foreignFiles.isEmpty())
			return;
		fileDragExit(m_foreignFiles);
		m_foreignFiles.clear();
	}

	void RmlDrag::itemDropped(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails)
	{
		if (m_foreignFiles.isEmpty())
			return;
		const auto files = m_foreignFiles;
		m_foreignFiles.clear();
		filesDropped(files, _dragSourceDetails.localPosition.x, _dragSourceDetails.localPosition.y);
	}

	bool RmlDrag::shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTarget::SourceDetails& _sourceDetails, juce::StringArray& _files, bool& _canMoveFiles)
	{
		if (!m_juceDraggable || !m_juceDraggable->data || !m_juceDraggable->data->canExportAsFiles())
			return false;

		std::vector<std::string> files;
		m_juceDraggable->data->getFilesForExport(files, _canMoveFiles);

		if (files.empty())
			return false;

		for (const auto& file : files)
			_files.add(juce::String(file));

		_canMoveFiles = false;
		m_externalDragStarted = true;
		return true;
	}

	bool RmlDrag::dragOperationEnded()
	{
		// The operating system's drag session that takes over from here swallows the mouse-up, the context would keep
		// dragging the element forever. Release it; the drop happens outside, where no element can take it.
		if (!m_externalDragStarted)
			return false;
		m_externalDragStarted = false;

		auto* context = m_component.getContext();
		context->ProcessMouseLeave();
		context->ProcessMouseButtonUp(0, 0);

		m_rmlDragSource = nullptr;
		m_juceDraggable = nullptr;
		return true;
	}

	void RmlDrag::processOutOfBoundsDrag(juce::Point<int>)
	{
		// we convert an rml drag to a juce drag if the rml drag source allows exporting files
		if (!m_rmlDragSource)
			return;

		auto* dd = m_rmlDragSource->getDragData();

		if (!dd || !dd->canExportAsFiles())
			return;

		if (m_juceDraggable)
			return;

		m_juceDraggable = new JuceDraggable();
		m_juceDraggable->data = dd;
		// to the windows of other instances too, see itemDragEnter
		m_component.startDragging(m_juceDraggable, &m_component, juce::ScaledImage(), true);
	}

	void RmlDrag::startDrag(int _x, int _y, const juce::StringArray& _files)
	{
		if (m_draggable)
			return;

		//  create a fake element that we can drag around. To start dragging we need to inject a mouse down and move event into the context.

		const auto pos = m_component.toRmlPosition(_x, _y);

		constexpr auto size = 100.0f;

		Rml::ElementPtr draggable = m_component.getDocument()->CreateElement("div");
		m_juceDragSource.reset(new FileDragSource(draggable.get(), _files));

		draggable->SetProperty(Rml::PropertyId::Position, Rml::Property(Rml::Style::Position::Absolute));

		draggable->SetProperty(Rml::PropertyId::Left, Rml::Property(static_cast<float>(pos.x) - size/2.0f, Rml::Unit::PX));
		draggable->SetProperty(Rml::PropertyId::Top, Rml::Property(static_cast<float>(pos.y) - size/2.0f, Rml::Unit::PX));
		draggable->SetProperty(Rml::PropertyId::Width, Rml::Property(size, Rml::Unit::PX));
		draggable->SetProperty(Rml::PropertyId::Height, Rml::Property(size, Rml::Unit::PX));

		draggable->SetProperty(Rml::PropertyId::ZIndex, Rml::Property(1000, Rml::Unit::NUMBER));

//		draggable->SetProperty(Rml::PropertyId::BackgroundColor, Rml::Property(Rml::Colourb(255, 0, 255, 255), Rml::Unit::COLOUR));

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

	void RmlDrag::ProcessEvent(Rml::Event& _event)
	{
		switch (_event.GetId())
		{
		case Rml::EventId::Dragstart:
			m_rmlDragSource = helper::getDragSource(_event);
			break;
		case Rml::EventId::Dragend:
			m_rmlDragSource = nullptr;
			m_juceDraggable = nullptr;
			break;
		default:;
		}
	}
}
