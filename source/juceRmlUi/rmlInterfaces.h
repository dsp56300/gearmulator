#pragma once

#include "rmlFileInterface.h"
#include "rmlSystemInterface.h"

#include "Core/FontEngineDefault/FontEngineInterfaceDefault.h"

namespace juceRmlUi
{
	struct RmlInterfaces
	{
	public:
		class ScopedAccess
		{
		public:
			ScopedAccess(RmlInterfaces& _rmlInterfaces) : m_rmlInterfaces(_rmlInterfaces)
			{
				m_rmlInterfaces.attach();
			}
			ScopedAccess(const ScopedAccess&) = delete;
			ScopedAccess(ScopedAccess&&) = default;
			~ScopedAccess()
			{
				m_rmlInterfaces.detach();
			}
		private:
			RmlInterfaces& m_rmlInterfaces;
		};

		explicit RmlInterfaces(genericUI::EditorInterface& _editorInterface);
		~RmlInterfaces();

		void attach();
		void detach();

		ScopedAccess getScopedAccess()
		{
			return { *this };
		}

	private:
		bool m_attached = false;

		SystemInterface m_systemInterface;
		Rml::FontEngineInterfaceDefault m_fontEngineInterface;
		FileInterface m_fileInterface;
	};
}
