#include "rmlInterfaces.h"

#include <mutex>

#include "RmlUi/Core/Core.h"

namespace juceRmlUi
{
	namespace
	{
		std::mutex g_accessMutex;
		uint32_t g_instanceCount = 0;
	}

	RmlInterfaces::RmlInterfaces(DataProvider& _dataProvider)
		: m_fileInterface(_dataProvider)
	{
		ScopedAccess access(getScopedAccess());

		if (++g_instanceCount == 1)
			Rml::Initialise();
	}

	RmlInterfaces::~RmlInterfaces()
	{
		ScopedAccess access(getScopedAccess());

		if (--g_instanceCount == 0)
			Rml::Shutdown();
	}

	void RmlInterfaces::attach()
	{
		g_accessMutex.lock();

		if (m_attached)
			return;

		Rml::SetSystemInterface(&m_systemInterface);
		Rml::SetFontEngineInterface(&m_fontEngineInterface);
		Rml::SetFileInterface(&m_fileInterface);

		m_attached = true;
	}

	void RmlInterfaces::detach()
	{
		if (!m_attached)
			return;

		Rml::SetSystemInterface(nullptr);
		Rml::SetFontEngineInterface(nullptr);
		Rml::SetFileInterface(nullptr);

		m_attached = false;

		g_accessMutex.unlock();
	}
}
