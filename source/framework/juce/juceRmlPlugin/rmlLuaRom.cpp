#include "rmlLuaRom.h"

#include <algorithm>
#include <optional>

#include "jucePluginLib/controller.h"
#include "jucePluginLib/processor.h"

#include "juceRmlUi/rmlInterfaces.h"

#include "RmlUi/Lua/IncludeLua.h"
#include "RmlUi/Lua/Interpreter.h"

#include "juce_events/juce_events.h"

namespace rmlPlugin
{
	static const char* const ROM_LISTENERS_REGISTRY_KEY = "RmlPlugin.RomListeners";

	namespace
	{
		LuaRomListeners& getListeners(lua_State* _L)
		{
			lua_getfield(_L, LUA_REGISTRYINDEX, ROM_LISTENERS_REGISTRY_KEY);
			auto* l = static_cast<LuaRomListeners*>(lua_touserdata(_L, -1));
			lua_pop(_L, 1);
			return *l;
		}

		// rom.getNames() -> array of names, empty for a product that has no list of ROMs to choose from
		int luaRomGetNames(lua_State* _L)
		{
			const auto& names = getListeners(_L).getProcessor().getFirmwareNames();

			lua_createtable(_L, static_cast<int>(names.size()), 0);
			for (size_t i = 0; i < names.size(); ++i)
			{
				lua_pushstring(_L, names[i].c_str());
				lua_rawseti(_L, -2, static_cast<lua_Integer>(i + 1));
			}
			return 1;
		}

		// rom.getSelected() -> name, index (0-based), nil if there is no list
		int luaRomGetSelected(lua_State* _L)
		{
			const auto& processor = getListeners(_L).getProcessor();
			const auto& names = processor.getFirmwareNames();
			const auto index = processor.getFirmwareIndex();

			if (index >= names.size())
			{
				lua_pushnil(_L);
				return 1;
			}

			lua_pushstring(_L, names[index].c_str());
			lua_pushinteger(_L, index);
			return 2;
		}

		// rom.select(name or index) -> bool, false if there is no such ROM or the device failed to start with it
		int luaRomSelect(lua_State* _L)
		{
			auto& processor = getListeners(_L).getProcessor();
			const auto& names = processor.getFirmwareNames();

			std::optional<uint32_t> index;

			if (lua_type(_L, 1) == LUA_TSTRING)
			{
				const auto it = std::find(names.begin(), names.end(), lua_tostring(_L, 1));
				if (it != names.end())
					index = static_cast<uint32_t>(it - names.begin());
			}
			else if (const auto i = luaL_checkinteger(_L, 1); i >= 0)
			{
				index = static_cast<uint32_t>(i);
			}

			lua_pushboolean(_L, index && processor.selectFirmware(*index));
			return 1;
		}

		// rom.onChange(func) -> id, func(name, index)
		int luaRomOnChange(lua_State* _L)
		{
			luaL_checktype(_L, 1, LUA_TFUNCTION);
			lua_pushvalue(_L, 1);
			const int funcRef = luaL_ref(_L, LUA_REGISTRYINDEX);

			lua_pushinteger(_L, getListeners(_L).add(funcRef));
			return 1;
		}

		// rom.removeListener(id)
		int luaRomRemoveListener(lua_State* _L)
		{
			getListeners(_L).remove(static_cast<int>(luaL_checkinteger(_L, 1)));
			return 0;
		}
	}

	LuaRomListeners::LuaRomListeners(lua_State* _luaState, pluginLib::Processor& _processor, juceRmlUi::RmlInterfaces& _rmlInterfaces)
		: m_luaState(_luaState)
		, m_processor(_processor)
		, m_rmlInterfaces(_rmlInterfaces)
	{
		// A state restored by the host switches the ROM on the host's thread, the callbacks belong to the message
		// thread. On the message thread they are picked now: the event is retained, and neither the call it makes when
		// this starts listening nor a change before a callback was registered is reported to it
		m_listener.set(m_processor.evFirmwareChanged, [this](const uint32_t _index)
		{
			std::optional<std::vector<int>> ids;

			if (juce::MessageManager::existsAndIsCurrentThread())
				ids = getIds();

			juce::MessageManager::callAsync([this, alive = std::weak_ptr(m_alive), _index, ids = std::move(ids)]
			{
				if (alive.expired())
					return;

				for (const auto id : ids ? *ids : getIds())
					invoke(id, _index);
			});
		});
	}

	LuaRomListeners::~LuaRomListeners()
	{
		removeAll();
	}

	int LuaRomListeners::add(const int _luaFuncRef)
	{
		const int id = m_nextId++;
		m_luaFuncRefs.emplace(id, _luaFuncRef);
		return id;
	}

	bool LuaRomListeners::remove(const int _id)
	{
		const auto it = m_luaFuncRefs.find(_id);

		if (it == m_luaFuncRefs.end())
			return false;

		luaL_unref(m_luaState, LUA_REGISTRYINDEX, it->second);
		m_luaFuncRefs.erase(it);
		return true;
	}

	void LuaRomListeners::removeAll()
	{
		for (const auto& [id, ref] : m_luaFuncRefs)
			luaL_unref(m_luaState, LUA_REGISTRYINDEX, ref);
		m_luaFuncRefs.clear();
	}

	std::vector<int> LuaRomListeners::getIds() const
	{
		std::vector<int> ids;
		for (const auto& [id, ref] : m_luaFuncRefs)
			ids.push_back(id);
		return ids;
	}

	void LuaRomListeners::invoke(const int _id, const uint32_t _index) const
	{
		// removed while the call was queued, or by an earlier callback for the same change
		const auto it = m_luaFuncRefs.find(_id);
		if (it == m_luaFuncRefs.end())
			return;

		// a product without a list has nothing to report, it fires after each reboot
		const auto& names = m_processor.getFirmwareNames();
		if (_index >= names.size())
			return;

		// the callback is free to change the DOM, which the render thread walks
		juceRmlUi::RmlInterfaces::ScopedAccess access(m_rmlInterfaces);

		lua_rawgeti(m_luaState, LUA_REGISTRYINDEX, it->second);
		lua_pushstring(m_luaState, names[_index].c_str());
		lua_pushinteger(m_luaState, _index);
		Rml::Lua::Interpreter::ExecuteCall(m_luaState, 2, 0);
	}

	void registerLuaRom(lua_State* _L, pluginLib::Controller& _controller, juceRmlUi::RmlInterfaces& _rmlInterfaces)
	{
		// a registration that was never undone would keep its listeners, and the callbacks they post, past the
		// end of the Lua state
		unregisterLuaRom(_L);

		lua_pushlightuserdata(_L, new LuaRomListeners(_L, _controller.getProcessor(), _rmlInterfaces));
		lua_setfield(_L, LUA_REGISTRYINDEX, ROM_LISTENERS_REGISTRY_KEY);

		static const luaL_Reg funcs[] = {
			{"getNames", luaRomGetNames},
			{"getSelected", luaRomGetSelected},
			{"select", luaRomSelect},
			{"onChange", luaRomOnChange},
			{"removeListener", luaRomRemoveListener},
			{nullptr, nullptr}
		};

		lua_newtable(_L);
		luaL_setfuncs(_L, funcs, 0);
		lua_setglobal(_L, "rom");
	}

	void unregisterLuaRom(lua_State* _L)
	{
		lua_getfield(_L, LUA_REGISTRYINDEX, ROM_LISTENERS_REGISTRY_KEY);
		delete static_cast<LuaRomListeners*>(lua_touserdata(_L, -1));
		lua_pop(_L, 1);

		lua_pushnil(_L);
		lua_setfield(_L, LUA_REGISTRYINDEX, ROM_LISTENERS_REGISTRY_KEY);

		lua_pushnil(_L);
		lua_setglobal(_L, "rom");
	}
}
