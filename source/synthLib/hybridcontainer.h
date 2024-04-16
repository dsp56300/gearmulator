#pragma once
/*
#include <vector>
#include <memory_resource>
#include <array>

namespace synthLib
{
	template<typename T, size_t S> class BufferResource
	{
	public:
		auto& getPool() { return m_pool; }
	protected:
		std::array<T, S> m_buffer;
		std::pmr::monotonic_buffer_resource m_pool{ std::data(m_buffer), std::size(m_buffer) };
	};

	template<typename T, size_t S>
	class HybridVector final : public BufferResource<T,S>, public std::pmr::vector<T>
	{
	public:
		using Base = std::pmr::vector<T>;

		HybridVector() : BufferResource<T, S>(), Base(&static_cast<BufferResource<T, S>&>(*this).getPool())
		{
		}
	};
}
*/