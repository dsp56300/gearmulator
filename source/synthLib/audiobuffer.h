#pragma once
#include <cstddef>
#include <vector>

namespace synthLib
{
	class AudioBuffer
	{
	public:
		using TChannel = std::vector<float>;
		using TBuffer = std::vector< TChannel >;

		void reserve(size_t _capacity);
		void resize(size_t _capacity);
		void append(const TBuffer& _data);
		void append(float** _data, size_t _size);

		void remove(size_t _count);
		
		void fillPointers(float** _pointers, size_t _offset = 0);
		size_t size() const;

		void ensureSize(size_t _size)
		{
			if(size() < _size)
				resize(_size);
		}

		void insertZeroes(size_t _size);
		const std::vector<float>& getChannel(const size_t _channel) { return m_data[_channel]; }
		bool empty() const { return size() == 0; }

		AudioBuffer(size_t _channelCount = 2, size_t _capacity = 1024);
	private:
		static void append(TChannel& _dst, const float* _data, size_t _size);
		TBuffer m_data;
	};
}
