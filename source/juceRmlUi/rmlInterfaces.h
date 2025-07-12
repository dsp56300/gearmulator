#pragma once

#include "rmlFileInterface.h"
#include "rmlSystemInterface.h"

#include "Core/FontEngineDefault/FontEngineInterfaceDefault.h"

#include "RmlUi/Core/ElementInstancer.h"

namespace juceRmlUi
{
	class RmlComponent;
	class DataProvider;

	struct RmlInterfaces : Rml::NonCopyMoveable
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

		template<typename T>
		static Rml::ElementInstancerGeneric<T>& getInstancer();

	private:
		uint32_t m_attached = 0;

		SystemInterface m_systemInterface;
		Rml::FontEngineInterfaceDefault m_fontEngineInterface;
		FileInterface m_fileInterface;
	};
}
