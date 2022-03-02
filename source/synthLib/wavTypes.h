#pragma once

#include <cstdint>

namespace synthLib
{
#ifdef _MSC_VER
	// TODO: gcc and others
#pragma pack(push, 1)
#endif

	struct SWaveFormatHeader
	{
		uint8_t			str_riff[4];			// ASCII string "RIFF"
		uint32_t		file_size;				// File size - 8
		uint8_t			str_wave[4];			// ASCII string "WAVE"
	};

	struct SWaveFormatChunkInfo
	{
		uint8_t			chunkName[4];			// chunk name
		uint32_t		chunkSize;				// chunk length
	};

	struct SWaveFormatChunkFormat				// "fmt ", size = 16 (0x10)
	{
		uint16_t		wave_type;				// PCM = 1
		uint16_t		num_channels;			// number of channels
		uint32_t		sample_rate;			// sample rate
		uint32_t		bytes_per_sec;			// bytes per sample (it's second, isn't it?!)
		uint16_t		block_alignment;		// bytes, that must be sent at a single time
		uint16_t		bits_per_sample;		// bits per sample
	};

	struct SWaveFormatChunkCue
	{
		uint32_t		cuePointCount;			// number of cue points in list
	};

	struct SWaveFormatChunkCuePoint
	{
		uint32_t		cueId;					// unique identification value
		uint32_t		playOrderPosition;		// play order position
		int8_t			dataChunkId[4];			// RIFF ID of corresponding data chunk
		uint32_t		chunkStart;				// byte offset of data chunk*
		uint32_t		blockStart;				// byte offset to sample of First Channel
		uint32_t		sampleOffset;			// byte offset to sample byte of First Channel
	};

	struct SWaveFormatChunkLabel
	{
		uint32_t		cuePointId;				// unique identification value
	};

	struct SWaveFormatChunkList
	{
		uint8_t			typeId[4];				// "adtl" (0x6164746C) => associated data list
	};

#ifdef _MSC_VER
#pragma pack(pop)
#endif

	enum EWaveFormat
	{
		eFormat_PCM							= 0x0001,
		eFormat_MS_ADPCM					= 0x0002,
		eFormat_IEEE_FLOAT					= 0x0003,
		eFormat_IBM_CVSD					= 0x0005,
		eFormat_ALAW						= 0x0006,
		eFormat_MULAW						= 0x0007,
		eFormat_OKI_ADPCM					= 0x0010,
		eFormat_DVI_IMA_ADPCM				= 0x0011,
		eFormat_MEDIASPACE_ADPCM			= 0x0012,
		eFormat_SIERRA_ADPCM				= 0x0013,
		eFormat_G723_ADPCM					= 0x0014,
		eFormat_DIGISTD						= 0x0015,
		eFormat_DIGIFIX						= 0x0016,
		eFormat_DIALOGIC_OKI_ADPCM			= 0x0017,
		eFormat_YAMAHA_ADPCM				= 0x0020,
		eFormat_SONARC						= 0x0021,
		eFormat_DSPGROUP_TRUESPEECH			= 0x0022,
		eFormat_ECHOSC1						= 0x0023,
		eFormat_AUDIOFILE_AF36				= 0x0024,
		eFormat_APTX						= 0x0025,
		eFormat_AUDIOFILE_AF10				= 0x0026,
		eFormat_DOLBY_AC2					= 0x0030,
		eFormat_GSM610						= 0x0031,
		eFormat_ANTEX_ADPCME				= 0x0033,
		eFormat_CONTROL_RES_VQLPC			= 0x0034,
		eFormat_CONTROL_RES_VQLPC_2			= 0x0035,
		eFormat_DIGIADPCM					= 0x0036,
		eFormat_CONTROL_RES_CR10			= 0x0037,
		eFormat_NMS_VBXADPCM				= 0x0038,
		eFormat_CS_IMAADPCM					= 0x0039,
		eFormat_G721_ADPCM					= 0x0040,
		eFormat_MPEG						= 0x0050,
		eFormat_Xbox_ADPCM					= 0x0069,
		eFormat_CREATIVE_ADPCM				= 0x0200,
		eFormat_CREATIVE_FASTSPEECH8		= 0x0202,
		eFormat_CREATIVE_FASTSPEECH10		= 0x0203,
		eFormat_FM_TOWNS_SND				= 0x0300,
		eFormat_OLIGSM						= 0x1000,
		eFormat_OLIADPCM					= 0x1001,
		eFormat_OLICELP						= 0x1002,
		eFormat_OLISBC						= 0x1003,
		eFormat_OLIOPR						= 0x1004
	};
}
