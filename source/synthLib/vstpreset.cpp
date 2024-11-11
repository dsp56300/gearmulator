#include "vstpreset.h"

#include "baseLib/binarystream.h"

#include "dsp56kEmu/logging.h"

namespace synthLib
{
	namespace
	{
		bool check4CC(const VstPreset::FourCC& _4CC, const char* _expected)
		{
			return 0 == strcmp(_4CC.data(), _expected);
		}
		bool check4CC(baseLib::BinaryStream& _stream, const char* _expected)
		{
			VstPreset::FourCC fourCC;
			_stream.read4CC(fourCC);
			return check4CC(fourCC, _expected);
		}
		template<VstPreset::Endian SourceEndian>
		uint32_t readUint32(baseLib::BinaryStream& _s)
		{
			const auto i = _s.read<uint32_t>();

			if constexpr (VstPreset::hostEndian() == SourceEndian)
				return i;
			return
				((i & 0xFF000000) >> 24) |
				((i & 0x00FF0000) >> 8) |
				((i & 0x0000FF00) << 8) |
				((i & 0x000000FF) << 24);
		}
        template<VstPreset::Endian SourceEndian>
        uint64_t readUint64(baseLib::BinaryStream& _s)
        {
            const auto i = _s.read<uint64_t>();

            if constexpr (VstPreset::hostEndian() == SourceEndian)
                return i;
            return
				((i & 0xFF00000000000000) >> 56) | 
				((i & 0x00FF000000000000) >> 40) | 
				((i & 0x0000FF0000000000) >> 24) |
				((i & 0x000000FF00000000) >> 8) |
                ((i & 0x00000000FF000000) << 8) | 
				((i & 0x0000000000FF0000) << 24) | 
				((i & 0x000000000000FF00) << 40) | 
				((i & 0x00000000000000FF) << 56);
        }
		uint32_t readUint32Vst2(baseLib::BinaryStream& _s)
		{
			return readUint32<VstPreset::Endian::Big>(_s);
		}
		uint32_t readUint32Vst3(baseLib::BinaryStream& _s)
		{
			return readUint32<VstPreset::Endian::Little>(_s);
		}
		uint64_t readUint64Vst3(baseLib::BinaryStream& _s)
		{
			return readUint64<VstPreset::Endian::Little>(_s);
		}
	}

	std::optional<VstPreset::ChunkList> VstPreset::read(const std::vector<uint8_t>& _data)
	{
		try
		{
			baseLib::BinaryStream binaryStream(_data);

			FourCC fourCC;
			binaryStream.read4CC(fourCC);

			if(check4CC(fourCC, "CcnK"))
			{
				binaryStream.setReadPos(0);
				return readFxbFxp(binaryStream);
			}
			if(check4CC(fourCC, "VST3"))
			{
				binaryStream.setReadPos(0);
				return readVst3(binaryStream);
			}
		}
		catch (const std::exception& e)
		{
			LOG("Error reading VstPreset: " << e.what());
		}
		return {};
	}

	std::optional<VstPreset::ChunkList> VstPreset::readFxbFxp(baseLib::BinaryStream& _binaryStream)
	{
		if(!check4CC(_binaryStream, "CcnK"))
			return {};
		/*const auto len = */readUint32Vst2(_binaryStream);
		FourCC dataType;
		_binaryStream.read4CC(dataType);

		ChunkList chunkList;

		// bank
		const auto isRegularBank = check4CC(dataType, "FxBk");
		const auto isOpaqueBank = check4CC(dataType, "FBCh");

		if(isRegularBank || isOpaqueBank)
		{
			const auto version = readUint32Vst2(_binaryStream);
			FourCC pluginId;
			_binaryStream.read4CC(pluginId);
			/*const auto pluginVersion = */readUint32Vst2(_binaryStream);
			const auto numPrograms = readUint32Vst2(_binaryStream);

			if(version >= 2)
			{
				/*const auto currentProgram = */(void)readUint32Vst2(_binaryStream);
				char future[124];
				_binaryStream.read(future, sizeof(future));
			}
			else
			{
				char future[128];
				_binaryStream.read(future, sizeof(future));
			}

			for(uint32_t i=0; i<numPrograms; ++i)
			{
				if(isOpaqueBank)
				{
					const auto chunkSize = readUint32Vst2(_binaryStream);
					if(!chunkSize)
						continue;

					Chunk c;
					c.data.resize(chunkSize);
					_binaryStream.read(c.data.data(), chunkSize);
					chunkList.push_back(c);
				}
				else
				{
					auto programChunks = readFxbFxp(_binaryStream);
					if(!programChunks)
						return {};
					if(!programChunks->empty())
						chunkList.insert(chunkList.end(), programChunks->begin(), programChunks->end());
				}
			}
			return chunkList;
		}

		// program
		const auto isRegularProgram = check4CC(dataType, "FxCk");
		const auto isOpaqueProgram = check4CC(dataType, "FPCh");

		if(isRegularProgram || isOpaqueProgram)
		{
			/*const auto version = */readUint32Vst2(_binaryStream);
			FourCC pluginId;
			_binaryStream.read4CC(pluginId);
			/*const auto pluginVersion = */readUint32Vst2(_binaryStream);

			const auto numParams = readUint32Vst2(_binaryStream);

			char programName[29]{};
			_binaryStream.read(programName, sizeof(programName)-1);

			if(isOpaqueProgram)
			{
				const auto chunkSize = readUint32Vst2(_binaryStream);
				if(!chunkSize)
					return {};

				Chunk c;
				c.data.resize(chunkSize);
				_binaryStream.read(c.data.data(), chunkSize);
				chunkList.push_back(c);
			}
			else
			{
				for (size_t i = 0; i < numParams; i++)
					_binaryStream.read<float>();
			}
			return chunkList;
		}
		return {};
	}

	std::optional<VstPreset::ChunkList> VstPreset::readVst3(baseLib::BinaryStream& _binaryStream)
	{
		if(!check4CC(_binaryStream, "VST3"))
			return {};

		const auto version = readUint32Vst3(_binaryStream);

		if(version != 1)
			return {};

		std::array<char, 32> classId;
		_binaryStream.read(classId);

		const auto chunkListOffset = readUint32Vst3(_binaryStream);

//		auto posChunkDataBegin = _binaryStream.getReadPos();

		_binaryStream.setReadPos(chunkListOffset);

		if(!check4CC(_binaryStream, "List"))
			return {};

		const auto chunkListEntryCount = readUint32Vst3(_binaryStream);

		ChunkList chunkList;

		for(uint32_t i=0; i<chunkListEntryCount; ++i)
		{
			Chunk c;
			_binaryStream.read4CC(c.id);

			const auto chunkOffset = readUint64Vst3(_binaryStream);
			const auto chunkSize = readUint64Vst3(_binaryStream);

			if(!chunkSize)
				continue;

			c.data.resize(chunkSize);

			const auto readPos = _binaryStream.getReadPos();
			_binaryStream.setReadPos(static_cast<uint32_t>(chunkOffset));

//			posChunkDataBegin += chunkSize;

			_binaryStream.read(c.data.data(), chunkSize);
			_binaryStream.setReadPos(readPos);

			// VST3 can embed a VST2 fxb/fxp, unpack it
			baseLib::BinaryStream bs(c.data);
			FourCC fourCC;
			bs.read4CC(fourCC);

			// according to vst2persistence.cpp of the VST3 SDK, this one is optional
			if(check4CC(fourCC, "VstW"))
			{
				const auto len = readUint32Vst2(bs);
				const auto vst2version = readUint32Vst2(bs);
				/*const auto bypassed = */readUint32Vst2(bs);

				assert(len == 8 && vst2version == 1);

				const auto vst2Chunks = readFxbFxp(bs);

				if(vst2Chunks)
					chunkList.insert(chunkList.end(), vst2Chunks->begin(), vst2Chunks->end());
			}
			else if(check4CC(fourCC, "CcnK"))
			{
				bs.setReadPos(0);

				const auto vst2Chunks = readFxbFxp(bs);

				if(vst2Chunks)
					chunkList.insert(chunkList.end(), vst2Chunks->begin(), vst2Chunks->end());
			}
			else
			{
				chunkList.push_back(c);
			}
		}
		return chunkList;
	}
}
