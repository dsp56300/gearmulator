#pragma once

#include "demoplayback.h"

namespace virusLib
{
	class Microcontroller;

	class DemoPlaybackTI : public DemoPlayback
	{
	public:
		DemoPlaybackTI(Microcontroller& _mc) : DemoPlayback(_mc) {}

		bool loadFile(const std::string& _filename) override;

	private:
		struct Chunk
		{
			uint32_t deltaTime;
			std::vector<uint8_t> data;
		};

		void parseData(const std::vector<uint8_t>& _data);
		bool parseChunk(const std::vector<uint8_t>& _data, size_t _offset);

		size_t getEventCount() const override { return m_chunks.size(); }
		uint32_t getEventDelay(const size_t _index) const override { return m_chunks[_index].deltaTime; }
		bool processEvent(const size_t _index) override	{ return processEvent(m_chunks[_index]); }

		bool processEvent(const Chunk& _chunk);

		std::vector<Chunk> m_chunks;
		int32_t m_remainingPresetBytes = 0;
	};
}
