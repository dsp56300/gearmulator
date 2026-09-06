#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>

#include "baseLib/event.h"

namespace baseLib
{
	class BinaryStream;
	class ChunkReader;
}

namespace pluginLib
{
	// Storage a skin can use for its own state, for things the emulator knows nothing about - which
	// knobs a skin has linked together, which of its pages was open, and so on. Two scopes:
	//
	//   Instance  travels with the plugin state, so it is part of the host project
	//   Global    is kept in the plugin config and is therefore the same for every instance
	//
	// Reading without naming a scope answers from the instance first and falls back to the global
	// value, so a skin can ship a global default and let a project override it.
	class SkinVariables
	{
	public:
		enum class Scope : uint8_t
		{
			Instance,
			Global
		};

		using Value = std::variant<int64_t, std::string>;

		// name of the variable that changed, and the scope it changed in
		baseLib::Event<std::string, Scope> evChanged;

		void set(const std::string& _name, const Value& _value, Scope _scope);

		// answers from _scope, or instance-then-global when no scope is given
		const Value* get(const std::string& _name) const;
		const Value* get(const std::string& _name, Scope _scope) const;

		bool remove(const std::string& _name, Scope _scope);

		const std::map<std::string, Value>& getAll(Scope _scope) const;

		// the instance scope is part of the plugin state
		void saveChunkData(baseLib::BinaryStream& _binaryStream) const;
		void loadChunkData(baseLib::ChunkReader& _cr);

		// The global scope is persisted by whoever owns a config file; it is set up from there at
		// startup without reporting a change, because nothing has had a chance to listen yet and a
		// skin must not see its own stored value arrive as an edit.
		void setGlobalsFromStorage(std::map<std::string, Value> _values);

		static std::string toString(const Value& _value);
		static Value fromString(const std::string& _string);

	private:
		std::map<std::string, Value>& getMap(Scope _scope);
		const std::map<std::string, Value>& getMap(Scope _scope) const;

		std::map<std::string, Value> m_instance;
		std::map<std::string, Value> m_global;
	};
}
