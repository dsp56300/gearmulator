#pragma once

#include "rmlFileInterface.h"
#include "rmlSystemInterface.h"

#include "Core/FontEngineDefault/FontEngineInterfaceDefault.h"

namespace juceRmlUi
{
	class DataProvider;

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

			ScopedAccess& operator=(const ScopedAccess&) = delete;
			ScopedAccess& operator=(ScopedAccess&&) = delete;

		private:
			RmlInterfaces& m_rmlInterfaces;
		};

		explicit RmlInterfaces(DataProvider& _dataProvider);
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
