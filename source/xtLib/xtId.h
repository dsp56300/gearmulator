#pragma once

#include <cstdint>
#include <limits>

namespace xt
{
	enum class IdType
	{
		WaveId,
		TableId,
		TableIndex
	};

	template<typename TId, IdType Type>
	class Id
	{
	public:
		using RawType = TId;
		static constexpr IdType MyIdType = Type;

		constexpr Id() : m_id(Invalid)
		{
		}
		constexpr explicit Id(const TId& _id) : m_id(_id)
		{
		}

		bool operator < (const Id& _id) const		{ return m_id < _id.m_id; }
		bool operator > (const Id& _id) const		{ return m_id > _id.m_id; }
		bool operator == (const Id& _id) const		{ return m_id == _id.m_id; }
		bool operator != (const Id& _id) const		{ return m_id != _id.m_id; }

		constexpr auto& rawId() const { return m_id; }

		constexpr static Id invalid() { return Id(Invalid); }
		constexpr bool isValid() const { return m_id != Invalid; }
		void invalidate() { m_id = Invalid; }

	private:
		static constexpr TId Invalid = std::numeric_limits<TId>::max();

		TId m_id;
	};

	using WaveId = Id<uint16_t, IdType::WaveId>;
	using TableId = Id<uint16_t, IdType::TableId>;
	using TableIndex = Id<uint16_t, IdType::TableIndex>;
}
