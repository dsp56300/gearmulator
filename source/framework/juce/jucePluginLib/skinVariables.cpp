#include "skinVariables.h"

#include "baseLib/binarystream.h"

namespace pluginLib
{
	namespace
	{
		// Values go into the config file as text, so the type has to survive the round trip: an int
		// written by a skin must not come back as the string "1".
		constexpr char g_prefixInt = 'i';
		constexpr char g_prefixString = 's';
	}

	void SkinVariables::set(const std::string& _name, const Value& _value, const Scope _scope)
	{
		if (_name.empty())
			return;

		auto& map = getMap(_scope);

		const auto it = map.find(_name);

		if (it != map.end() && it->second == _value)
			return;

		map[_name] = _value;

		evChanged(_name, _scope);
	}

	const SkinVariables::Value* SkinVariables::get(const std::string& _name) const
	{
		if (const auto* v = get(_name, Scope::Instance))
			return v;
		return get(_name, Scope::Global);
	}

	const SkinVariables::Value* SkinVariables::get(const std::string& _name, const Scope _scope) const
	{
		const auto& map = getMap(_scope);
		const auto it = map.find(_name);
		return it == map.end() ? nullptr : &it->second;
	}

	bool SkinVariables::remove(const std::string& _name, const Scope _scope)
	{
		auto& map = getMap(_scope);

		if (!map.erase(_name))
			return false;

		evChanged(_name, _scope);
		return true;
	}

	const std::map<std::string, SkinVariables::Value>& SkinVariables::getAll(const Scope _scope) const
	{
		return getMap(_scope);
	}

	void SkinVariables::saveChunkData(baseLib::BinaryStream& _binaryStream) const
	{
		if (m_instance.empty())
			return;

		baseLib::ChunkWriter cw(_binaryStream, "SKNV", 1);

		_binaryStream.write(static_cast<uint32_t>(m_instance.size()));

		for (const auto& [name, value] : m_instance)
		{
			_binaryStream.write(name);
			_binaryStream.write(toString(value));
		}
	}

	void SkinVariables::loadChunkData(baseLib::ChunkReader& _cr)
	{
		_cr.add("SKNV", 1, [this](baseLib::BinaryStream& _binaryStream, uint32_t)
		{
			m_instance.clear();

			const auto count = _binaryStream.read<uint32_t>();

			for (uint32_t i = 0; i < count; ++i)
			{
				auto name = _binaryStream.readString();
				auto value = fromString(_binaryStream.readString());

				if (name.empty())
					continue;

				m_instance.insert_or_assign(name, std::move(value));
			}

			// a state load replaces the whole instance scope, so tell listeners about all of it
			for (const auto& [name, value] : m_instance)
				evChanged(name, Scope::Instance);
		});
	}

	void SkinVariables::setGlobalsFromStorage(std::map<std::string, Value> _values)
	{
		m_global = std::move(_values);
	}

	std::string SkinVariables::toString(const Value& _value)
	{
		if (const auto* i = std::get_if<int64_t>(&_value))
			return g_prefixInt + std::to_string(*i);
		return g_prefixString + std::get<std::string>(_value);
	}

	SkinVariables::Value SkinVariables::fromString(const std::string& _string)
	{
		if (_string.empty())
			return std::string();

		const auto payload = _string.substr(1);

		if (_string.front() == g_prefixInt)
		{
			try
			{
				return static_cast<int64_t>(std::stoll(payload));
			}
			catch (...)
			{
				return static_cast<int64_t>(0);
			}
		}

		if (_string.front() == g_prefixString)
			return payload;

		// no prefix: written by something that did not go through toString, take it as a string
		return _string;
	}

	std::map<std::string, SkinVariables::Value>& SkinVariables::getMap(const Scope _scope)
	{
		return _scope == Scope::Global ? m_global : m_instance;
	}

	const std::map<std::string, SkinVariables::Value>& SkinVariables::getMap(const Scope _scope) const
	{
		return _scope == Scope::Global ? m_global : m_instance;
	}
}
