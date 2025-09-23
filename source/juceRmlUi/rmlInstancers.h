#pragma once

#include <tuple>
#include <type_traits>
#include <variant>

#include "RmlUi/Core/ElementInstancer.h"

namespace juceRmlUi
{
	template<class ...Args> class GenericInstancers
	{
	public:
		using MyTypes = std::tuple<Args...>;
		using Instancers = std::tuple<Rml::ElementInstancerGeneric<Args>...>;
		static constexpr size_t NumInstancers = sizeof...(Args);
		using Names = std::array<const char*, NumInstancers>;

		template<typename... NameArgs, typename = std::enable_if_t<sizeof...(NameArgs) == NumInstancers && (std::conjunction_v<std::is_convertible<NameArgs, const char*>...>)>>
		explicit GenericInstancers(NameArgs&&... _nameArgs)
		: m_names{ { std::forward<NameArgs>(_nameArgs)... } }
		{
		}

		template<typename T>
		Rml::ElementInstancerGeneric<T>& getInstancer()
		{
			return std::get<Rml::ElementInstancerGeneric<T>>(m_instancers);
		}

		template<size_t I>
		const char* getName()
		{
			static_assert(I < NumInstancers);
			return m_names[I];
		}

		const Instancers& getInstancers() const
		{
			return m_instancers;
		}

	private:
		Instancers m_instancers;
		Names m_names;
	};

	class CallbackElemInstancer final : public Rml::ElementInstancer
	{
	public:
		using Callback = std::function<Rml::ElementPtr(Rml::CoreInstance&, const Rml::String&)>;

		CallbackElemInstancer(Callback _callback) : m_callback(std::move(_callback)) {}

		Rml::ElementPtr InstanceElement(Rml::CoreInstance& _coreInstance, Rml::Element* _parent, const Rml::String& _tag, const Rml::XMLAttributes&) override
		{
			return m_callback(_coreInstance, _tag);
		}
		void ReleaseElement(Rml::CoreInstance&, Rml::Element* _element) override
		{
			delete _element;
		}

	private:
		Callback m_callback;
	};
}
