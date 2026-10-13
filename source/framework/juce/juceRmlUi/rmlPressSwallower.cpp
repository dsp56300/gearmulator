#include "rmlPressSwallower.h"

#include "rmlHelper.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/EventListener.h"

namespace juceRmlUi
{
	namespace
	{
		class PressSwallower final : public Rml::EventListener
		{
		public:
			static void add(Rml::Element* _document)
			{
				auto* swallower = new PressSwallower(_document);
				_document->AddEventListener(Rml::EventId::Mousedown, swallower, true);
				_document->AddEventListener(Rml::EventId::Mouseup, swallower, true);
				_document->AddEventListener(Rml::EventId::Click, swallower, true);
			}

			void ProcessEvent(Rml::Event& _event) override
			{
				switch (_event.GetId())
				{
				case Rml::EventId::Mouseup:
					if (helper::getMouseButton(_event) != MouseButton::Left)
						return;
					m_released = true;
					_event.StopPropagation();
					break;
				case Rml::EventId::Click:
					_event.StopPropagation();
					remove();
					break;
				case Rml::EventId::Mousedown:
					// Never the press that added this: listeners are collected before an event is
					// dispatched. Once that press has been released, the next one is somebody else's.
					if (m_released)
						remove();
					break;
				default:
					break;
				}
			}

			void OnAttach(Rml::Element*) override { ++m_attachCount; }

			// Removing it and the document dying both end here, once per event.
			void OnDetach(Rml::Element*) override
			{
				if (--m_attachCount == 0)
					delete this;
			}

		private:
			explicit PressSwallower(Rml::Element* _document) : m_document(_document) {}

			void remove()
			{
				// The last of these deletes this, so the member is copied first.
				auto* document = m_document;
				document->RemoveEventListener(Rml::EventId::Mousedown, this, true);
				document->RemoveEventListener(Rml::EventId::Mouseup, this, true);
				document->RemoveEventListener(Rml::EventId::Click, this, true);
			}

			Rml::Element* const m_document;
			uint32_t m_attachCount = 0;
			bool m_released = false;
		};
	}

	void swallowRestOfPress(Rml::Element* _document)
	{
		if (_document)
			PressSwallower::add(_document);
	}
}
