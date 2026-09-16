#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include "rmlFileInterface.h"
#include "rmlSystemInterface.h"

#include "Core/FontEngineDefault/FontEngineInterfaceDefault.h"
#include "RmlUi/Core/CoreInstance.h"

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

		auto& getCoreInstance() { return m_coreInstance; }
		SystemInterface& getSystemInterface() { return m_systemInterface; }

		// Registers the operating system's fallback fonts with this instance, once: the settings
		// window rebuilds its component on the same instance, and registering them again would
		// duplicate the faces. Call with access held.
		void loadSystemFallbackFonts();

		template<typename T>
		static Rml::ElementInstancerGeneric<T>& getInstancer();

	private:
		// Declared first so it is destroyed last: RmlUi reads these fonts until the instance has shut down.
		std::vector<std::shared_ptr<const void>> m_systemFallbackFonts;
		uint32_t m_attached = 0;
		Rml::CoreInstance m_coreInstance;
		SystemInterface m_systemInterface;
		Rml::FontEngineInterfaceDefault m_fontEngineInterface;
		FileInterface m_fileInterface;
		std::recursive_mutex m_accessMutex;
		bool m_systemFallbackFontsLoaded = false;
	};
}
