#include "rmlLuaParameters.h"

#include "jucePluginLib/controller.h"
#include "jucePluginLib/parameter.h"

#include "RmlUi/Core/Log.h"
#include "RmlUi/Lua/IncludeLua.h"

#include "juce_events/juce_events.h"

namespace rmlPlugin
{
	static const char* const CONTROLLER_REGISTRY_KEY = "RmlPlugin.Controller";
	static const char* const LISTENERS_REGISTRY_KEY = "RmlPlugin.LuaParameterListeners";

	static pluginLib::Controller& getController(lua_State* _L)
	{
		lua_getfield(_L, LUA_REGISTRYINDEX, CONTROLLER_REGISTRY_KEY);
		auto* c = static_cast<pluginLib::Controller*>(lua_touserdata(_L, -1));
		lua_pop(_L, 1);
		return *c;
	}

	static LuaParameterListeners& getListeners(lua_State* _L)
	{
		lua_getfield(_L, LUA_REGISTRYINDEX, LISTENERS_REGISTRY_KEY);
		auto* l = static_cast<LuaParameterListeners*>(lua_touserdata(_L, -1));
		lua_pop(_L, 1);
		return *l;
	}

	static uint8_t resolvePart(lua_State* _L, int _argIndex, pluginLib::Controller& _controller, bool& _isCurrentPart)
	{
		_isCurrentPart = false;
		if (lua_isstring(_L, _argIndex))
		{
			const char* str = lua_tostring(_L, _argIndex);
			if (strcmp(str, "current") == 0)
			{
				_isCurrentPart = true;
				return _controller.getCurrentPart();
			}
		}
		return static_cast<uint8_t>(luaL_checkinteger(_L, _argIndex));
	}

	static uint8_t resolvePart(lua_State* _L, int _argIndex, pluginLib::Controller& _controller)
	{
		bool isCurrent;
		return resolvePart(_L, _argIndex, _controller, isCurrent);
	}

	static uint8_t resolveOptionalPart(lua_State* _L, int _argIndex, pluginLib::Controller& _controller)
	{
		if (lua_gettop(_L) < _argIndex || lua_isnoneornil(_L, _argIndex))
			return _controller.getCurrentPart();
		return resolvePart(_L, _argIndex, _controller);
	}

	// params.get(name [, part_or_current]) -> int
	static int luaParamsGet(lua_State* _L)
	{
		const char* name = luaL_checkstring(_L, 1);
		auto& controller = getController(_L);
		const auto part = resolveOptionalPart(_L, 2, controller);

		auto* param = controller.getParameter(name, part);
		if (!param)
			return luaL_error(_L, "Parameter '%s' not found", name);

		lua_pushinteger(_L, param->getUnnormalizedValue());
		return 1;
	}

	// params.getText(name [, part_or_current]) -> string
	static int luaParamsGetText(lua_State* _L)
	{
		const char* name = luaL_checkstring(_L, 1);
		auto& controller = getController(_L);
		const auto part = resolveOptionalPart(_L, 2, controller);

		auto* param = controller.getParameter(name, part);
		if (!param)
			return luaL_error(_L, "Parameter '%s' not found", name);

		const auto text = param->getCurrentValueAsText().toStdString();
		lua_pushstring(_L, text.c_str());
		return 1;
	}

	// params.set(name, value [, part_or_current])
	static int luaParamsSet(lua_State* _L)
	{
		const char* name = luaL_checkstring(_L, 1);
		auto& controller = getController(_L);
		const auto value = static_cast<int>(luaL_checkinteger(_L, 2));
		const auto part = resolveOptionalPart(_L, 3, controller);

		auto* param = controller.getParameter(name, part);
		if (!param)
			return luaL_error(_L, "Parameter '%s' not found", name);

		param->setUnnormalizedValueNotifyingHost(value, pluginLib::Parameter::Origin::Ui);
		return 0;
	}

	// params.getInfo(name) -> table{min, max, name, displayName}
	static int luaParamsGetInfo(lua_State* _L)
	{
		const char* name = luaL_checkstring(_L, 1);
		auto& controller = getController(_L);

		const auto idx = controller.getParameterIndexByName(name);
		if (idx == pluginLib::Controller::InvalidParameterIndex)
			return luaL_error(_L, "Parameter '%s' not found", name);

		auto* param = controller.getParameter(idx, 0);
		if (!param)
			return luaL_error(_L, "Parameter '%s' not found", name);

		const auto& desc = param->getDescription();

		lua_newtable(_L);
		lua_pushinteger(_L, desc.range.getStart());
		lua_setfield(_L, -2, "min");
		lua_pushinteger(_L, desc.range.getEnd());
		lua_setfield(_L, -2, "max");
		lua_pushstring(_L, desc.name.c_str());
		lua_setfield(_L, -2, "name");
		lua_pushstring(_L, desc.displayName.c_str());
		lua_setfield(_L, -2, "displayName");
		lua_pushinteger(_L, desc.defaultValue);
		lua_setfield(_L, -2, "defaultValue");
		lua_pushboolean(_L, desc.isBool);
		lua_setfield(_L, -2, "isBool");
		lua_pushboolean(_L, desc.isBipolar);
		lua_setfield(_L, -2, "isBipolar");

		return 1;
	}

	// params.onChange(name, func [, part_or_current]) -> id
	static int luaParamsOnChange(lua_State* _L)
	{
		const char* name = luaL_checkstring(_L, 1);
		auto& controller = getController(_L);
		luaL_checktype(_L, 2, LUA_TFUNCTION);

		bool isCurrentPart = false;
		uint8_t part;

		if (lua_gettop(_L) >= 3 && !lua_isnoneornil(_L, 3))
			part = resolvePart(_L, 3, controller, isCurrentPart);
		else
		{
			part = controller.getCurrentPart();
			isCurrentPart = true;
		}

		auto* param = controller.getParameter(name, part);
		if (!param)
			return luaL_error(_L, "Parameter '%s' not found", name);

		lua_pushvalue(_L, 2);
		const int funcRef = luaL_ref(_L, LUA_REGISTRYINDEX);

		auto& listeners = getListeners(_L);
		const int id = listeners.addParameterListener(name, part, isCurrentPart, funcRef);

		lua_pushinteger(_L, id);
		return 1;
	}

	// params.removeListener(id)
	static int luaParamsRemoveListener(lua_State* _L)
	{
		const int id = static_cast<int>(luaL_checkinteger(_L, 1));
		auto& listeners = getListeners(_L);
		listeners.removeListener(id);
		return 0;
	}

	// params.getCurrentPart() -> int
	static int luaParamsGetCurrentPart(lua_State* _L)
	{
		auto& controller = getController(_L);
		lua_pushinteger(_L, controller.getCurrentPart());
		return 1;
	}

	// params.onPartChanged(func) -> id
	static int luaParamsOnPartChanged(lua_State* _L)
	{
		luaL_checktype(_L, 1, LUA_TFUNCTION);
		lua_pushvalue(_L, 1);
		const int funcRef = luaL_ref(_L, LUA_REGISTRYINDEX);

		auto& listeners = getListeners(_L);
		const int id = listeners.addPartChangedListener(funcRef);

		lua_pushinteger(_L, id);
		return 1;
	}

	// --- LuaParameterListeners ---

	static void invokeParamCallback(lua_State* _L, int _funcRef, int _value, const std::string& _text)
	{
		lua_rawgeti(_L, LUA_REGISTRYINDEX, _funcRef);
		lua_pushinteger(_L, _value);
		lua_pushstring(_L, _text.c_str());
		if (lua_pcall(_L, 2, 0, 0) != LUA_OK)
		{
			const char* err = lua_tostring(_L, -1);
			if (err)
				Rml::Log::Message(Rml::Log::LT_WARNING, "Lua parameter callback error: %s", err);
			lua_pop(_L, 1);
		}
	}

	LuaParameterListeners::LuaParameterListeners(lua_State* _L, pluginLib::Controller& _controller)
		: m_luaState(_L)
		, m_controller(_controller)
		, m_lastPart(_controller.getCurrentPart())
	{
		m_currentPartListener.set(_controller.onCurrentPartChanged, [this](const uint8_t& _part)
		{
			rebindCurrentPartListeners(_part);
		});
	}

	LuaParameterListeners::~LuaParameterListeners()
	{
		removeAllListeners();
	}

	int LuaParameterListeners::addParameterListener(const std::string& _name, uint8_t _part, bool _isCurrentPart, int _luaFuncRef)
	{
		const int id = m_nextId++;

		auto* param = m_controller.getParameter(_name, _part);
		if (!param)
			return -1;

		ParamListenerEntry entry;
		entry.luaFuncRef = _luaFuncRef;
		entry.paramName = _name;
		entry.isCurrentPart = _isCurrentPart;
		auto* L = m_luaState;

		auto [it, _] = m_paramListeners.emplace(id, std::move(entry));

		it->second.listener.set(param, [L, _luaFuncRef](pluginLib::Parameter* _p)
		{
			const int value = _p->getUnnormalizedValue();
			const auto text = _p->getCurrentValueAsText().toStdString();

			juce::MessageManager::callAsync([L, _luaFuncRef, value, text]()
			{
				invokeParamCallback(L, _luaFuncRef, value, text);
			});
		});

		return id;
	}

	int LuaParameterListeners::addPartChangedListener(int _luaFuncRef)
	{
		const int id = m_nextId++;

		auto* L = m_luaState;

		PartChangedEntry entry;
		entry.luaFuncRef = _luaFuncRef;
		entry.listener.set(m_controller.onCurrentPartChanged, [L, _luaFuncRef](const uint8_t& _part)
		{
			juce::MessageManager::callAsync([L, _luaFuncRef, _part]()
			{
				lua_rawgeti(L, LUA_REGISTRYINDEX, _luaFuncRef);
				lua_pushinteger(L, _part);
				if (lua_pcall(L, 1, 0, 0) != LUA_OK)
				{
					const char* err = lua_tostring(L, -1);
					if (err)
						Rml::Log::Message(Rml::Log::LT_WARNING, "Lua part changed callback error: %s", err);
					lua_pop(L, 1);
				}
			});
		});

		m_partChangedListeners.emplace(id, std::move(entry));
		return id;
	}

	void LuaParameterListeners::removeListener(int _id)
	{
		{
			auto it = m_paramListeners.find(_id);
			if (it != m_paramListeners.end())
			{
				luaL_unref(m_luaState, LUA_REGISTRYINDEX, it->second.luaFuncRef);
				m_paramListeners.erase(it);
				return;
			}
		}
		{
			auto it = m_partChangedListeners.find(_id);
			if (it != m_partChangedListeners.end())
			{
				luaL_unref(m_luaState, LUA_REGISTRYINDEX, it->second.luaFuncRef);
				m_partChangedListeners.erase(it);
			}
		}
	}

	void LuaParameterListeners::removeAllListeners()
	{
		for (auto& [id, entry] : m_paramListeners)
			luaL_unref(m_luaState, LUA_REGISTRYINDEX, entry.luaFuncRef);
		m_paramListeners.clear();

		for (auto& [id, entry] : m_partChangedListeners)
			luaL_unref(m_luaState, LUA_REGISTRYINDEX, entry.luaFuncRef);
		m_partChangedListeners.clear();
	}

	void LuaParameterListeners::rebindCurrentPartListeners(uint8_t _newPart)
	{
		const auto oldPart = m_lastPart;
		m_lastPart = _newPart;

		for (auto& [id, entry] : m_paramListeners)
		{
			if (!entry.isCurrentPart)
				continue;

			auto* param = m_controller.getParameter(entry.paramName, _newPart);
			if (!param)
				continue;

			const int funcRef = entry.luaFuncRef;
			auto* L = m_luaState;

			entry.listener.set(param, [L, funcRef](pluginLib::Parameter* _p)
			{
				const int value = _p->getUnnormalizedValue();
				const auto text = _p->getCurrentValueAsText().toStdString();

				juce::MessageManager::callAsync([L, funcRef, value, text]()
				{
					invokeParamCallback(L, funcRef, value, text);
				});
			});

			// Fire callback if the value differs between old and new part
			auto* oldParam = m_controller.getParameter(entry.paramName, oldPart);
			if (!oldParam || oldParam->getUnnormalizedValue() != param->getUnnormalizedValue())
			{
				const int value = param->getUnnormalizedValue();
				const auto text = param->getCurrentValueAsText().toStdString();
				juce::MessageManager::callAsync([L, funcRef, value, text]()
				{
					invokeParamCallback(L, funcRef, value, text);
				});
			}
		}
	}

	// --- Registration ---

	void registerLuaParameters(lua_State* _L, pluginLib::Controller& _controller)
	{
		// Store controller in registry
		lua_pushlightuserdata(_L, &_controller);
		lua_setfield(_L, LUA_REGISTRYINDEX, CONTROLLER_REGISTRY_KEY);

		// Create and store listeners manager
		auto* listeners = new LuaParameterListeners(_L, _controller);
		lua_pushlightuserdata(_L, listeners);
		lua_setfield(_L, LUA_REGISTRYINDEX, LISTENERS_REGISTRY_KEY);

		// Register the "params" global table
		static const luaL_Reg paramsFuncs[] = {
			{"get", luaParamsGet},
			{"getText", luaParamsGetText},
			{"set", luaParamsSet},
			{"getInfo", luaParamsGetInfo},
			{"onChange", luaParamsOnChange},
			{"removeListener", luaParamsRemoveListener},
			{"getCurrentPart", luaParamsGetCurrentPart},
			{"onPartChanged", luaParamsOnPartChanged},
			{nullptr, nullptr}
		};

		lua_newtable(_L);
		luaL_setfuncs(_L, paramsFuncs, 0);
		lua_setglobal(_L, "params");
	}

	void unregisterLuaParameters(lua_State* _L)
	{
		lua_getfield(_L, LUA_REGISTRYINDEX, LISTENERS_REGISTRY_KEY);
		auto* listeners = static_cast<LuaParameterListeners*>(lua_touserdata(_L, -1));
		lua_pop(_L, 1);

		if (!listeners)
			return;

		delete listeners;

		lua_pushnil(_L);
		lua_setfield(_L, LUA_REGISTRYINDEX, LISTENERS_REGISTRY_KEY);
		lua_pushnil(_L);
		lua_setfield(_L, LUA_REGISTRYINDEX, CONTROLLER_REGISTRY_KEY);
		lua_pushnil(_L);
		lua_setglobal(_L, "params");
	}
}
