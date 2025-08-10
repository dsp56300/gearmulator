#include "rmlInterfaces.h"

#include <mutex>

#include "juceRmlComponent.h"

#include "rmlElemButton.h"
#include "rmlElemCanvas.h"
#include "rmlElemComboBox.h"
#include "rmlElemKnob.h"
#include "rmlElemList.h"
#include "rmlElemListEntry.h"
#include "rmlElemSplitter.h"
#include "rmlElemTree.h"
#include "rmlElemTreeNode.h"
#include "rmlInstancers.h"

#include "RmlUi/Core/Core.h"
#include "RmlUi/Core/Factory.h"
#include "RmlUi/Core/PropertyDefinition.h"
#include "RmlUi/Core/StyleSheetSpecification.h"

namespace juceRmlUi
{
	namespace
	{
		std::recursive_mutex g_accessMutex;
		uint32_t g_instanceCount = 0;

		GenericInstancers<ElemButton, ElemCanvas, ElemComboBox, ElemKnob, ElemList, ElemListEntry, ElemSplitter, ElemTree, ElemTreeNode> g_instancers
		                 ("button",   "canvas",   "combo",      "knob",   "list",   "listentry",   "splitter"  , "tree",   "treenode"  );

		RmlComponent* g_currentComponent;

		template<size_t... I>
		void registerOne(Rml::Factory& _factory, std::index_sequence<I...>)
		{
			using Tuple = decltype(g_instancers)::MyTypes;
			(_factory.RegisterElementInstancer(g_instancers.getName<I>(), &RmlInterfaces::getInstancer<std::tuple_element_t<I, Tuple>>()), ...);
		}

		void registerInstancers(Rml::Factory& _factory)
		{
			using T = decltype(g_instancers)::MyTypes;
			constexpr size_t numInstancers = std::tuple_size_v<T>;
			registerOne(_factory, std::make_index_sequence<numInstancers>{});
		}
	}

	RmlInterfaces::ScopedAccess::ScopedAccess(RmlComponent& _component): m_rmlInterfaces(_component.getInterfaces())
	{
		m_rmlInterfaces.attach(&_component);
	}

	RmlInterfaces::ScopedAccess::ScopedAccess(RmlInterfaces& _interfaces) : m_rmlInterfaces(_interfaces)
	{
		m_rmlInterfaces.attach(nullptr);
	}

	RmlInterfaces::RmlInterfaces(DataProvider& _dataProvider)
		: m_fontEngineInterface(m_coreInstance)
		, m_fileInterface(_dataProvider)
	{
		ScopedAccess access(*this);

		if (++g_instanceCount == 1)
		{
			Rml::Initialise(m_coreInstance);

			Rml::SetSystemInterface(m_coreInstance, &m_systemInterface);
			Rml::SetFontEngineInterface(m_coreInstance, &m_fontEngineInterface);
			Rml::SetFileInterface(m_coreInstance, &m_fileInterface);

			auto& sss = *m_coreInstance.styleSheetSpecification;

			// button style elements
			sss.RegisterProperty("isToggle", "1", false).AddParser("number");
			sss.RegisterProperty("valueOn", "1", false).AddParser("number");
			sss.RegisterProperty("valueOff", "-1", false).AddParser("number");

			// knob style elements
			sss.RegisterProperty("frames", "128", false).AddParser("number");
			sss.RegisterProperty("speed", "0.01", false).AddParser("number");
			sss.RegisterProperty("spriteprefix", "frame", false).AddParser("string");
			sss.RegisterProperty("items-per-column", "16", false).AddParser("number");

			// tree style elements
			sss.RegisterProperty("indent-margin-left", "0", false).AddParser("length");
			sss.RegisterProperty("indent-padding-left", "0", false).AddParser("length");

			registerInstancers(*m_coreInstance.factory);
		}
	}

	RmlInterfaces::~RmlInterfaces()
	{
		ScopedAccess access(*this);

		if (--g_instanceCount == 0)
		{
			Rml::Shutdown(m_coreInstance);
		}
	}

	void RmlInterfaces::attach(RmlComponent* _component)
	{
		g_accessMutex.lock();

		if (++m_attached > 1)
			return;

		assert(!g_currentComponent || g_currentComponent == _component);

		g_currentComponent = _component;
	}

	void RmlInterfaces::detach()
	{
		if (--m_attached > 0)
			return;

		g_currentComponent = nullptr;

		g_accessMutex.unlock();
	}

	juceRmlUi::RmlComponent& RmlInterfaces::getCurrentComponent()
	{
		std::scoped_lock lock(g_accessMutex);
		assert(g_currentComponent != nullptr && "RmlInterfaces::getCurrentComponent: No current component set");
		return *g_currentComponent;
	}

	template <typename T> Rml::ElementInstancerGeneric<T>& RmlInterfaces::getInstancer()
	{
		return g_instancers.getInstancer<T>();
	}
}
