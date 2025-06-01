#pragma once

#include "rmlFileInterface.h"
#include "rmlSystemInterface.h"

#include "Core/FontEngineDefault/FontEngineInterfaceDefault.h"

namespace juceRmlUi
{
	class RmlComponent;
	class DataProvider;

	struct RmlInterfaces
	{
	public:
		class ScopedAccess
		{
		public:
			explicit ScopedAccess(RmlComponent& _component);
			explicit ScopedAccess(RmlInterfaces& _interfaces);
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

		void attach(RmlComponent* _component);
		void detach();

		static RmlComponent& getCurrentComponent();

		SystemInterface& getSystemInterface() { return m_systemInterface; }

	private:
		bool m_attached = false;

		SystemInterface m_systemInterface;
		Rml::FontEngineInterfaceDefault m_fontEngineInterface;
		FileInterface m_fileInterface;
	};
}
