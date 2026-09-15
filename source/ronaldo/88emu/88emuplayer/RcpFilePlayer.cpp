#include "RcpFilePlayer.h"

#include "midifile.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{
	constexpr std::size_t rcpHeaderSize = 0x586;
	constexpr std::size_t rcpTrackHeaderSize = 0x2c;
	constexpr std::size_t rcpEventSize = 4;
	constexpr std::size_t maximumTrackCount = 36;
	constexpr std::size_t maximumTrackEvents = 250000;
	constexpr std::size_t maximumInterpreterSteps = 4000000;
	constexpr std::size_t maximumOutputEvents = 2000000;
	constexpr int defaultLoopCount = 2;

	const char rcpHeaderPrefix[] = "RCM-PC98V2.0(C)COME ON MUSIC";

	struct RcpEvent
	{
		std::uint8_t command;
		std::uint8_t delay;
		std::uint8_t param1;
		std::uint8_t param2;
		std::size_t offset;
	};

	struct RcpTrack
	{
		int channel = -1;
		std::uint8_t port = 0;
		int transposition = 0;
		int startTick = 0;
		std::uint8_t rawStartTick = 0;
		bool muted = false;
		std::vector<RcpEvent> events;
	};

	struct RcpDocument
	{
		int timeBase = 48;
		int tempoBpm = 120;
		int beatNumerator = 4;
		int beatDenominator = 4;
		bool hasExplicitTrackCount = false;
		std::vector<std::vector<std::uint8_t>> userSysEx;
		std::vector<RcpTrack> tracks;
	};

	struct TimedEvent
	{
		std::int64_t tick;
		std::uint64_t order;
		std::uint8_t port = 0;
		std::vector<std::uint8_t> bytes;
	};

	struct TempoModifier
	{
		std::int64_t tick;
		std::uint64_t order;
		int ratio;
		int gradation;
	};

	struct TempoPoint
	{
		std::int64_t tick;
		std::uint64_t order;
		double bpm;
	};

	struct ActiveNote
	{
		bool active = false;
		std::int64_t offTick = 0;
		int channel = 0;
	};

	bool readLittleEndian16(const std::vector<std::uint8_t>& data, std::size_t offset, std::uint16_t& value)
	{
		if (offset > data.size() || data.size() - offset < 2)
			return false;

		value = static_cast<std::uint16_t>(data[offset]) | static_cast<std::uint16_t>(data[offset + 1] << 8);
		return true;
	}

	int signedSevenBit(std::uint8_t value)
	{
		return (value & 0x40) != 0 ? static_cast<int>(value) - 0x80 : static_cast<int>(value);
	}

	bool canAdvance(std::int64_t value, std::int64_t amount)
	{
		return amount <= 0 || value <= std::numeric_limits<std::int64_t>::max() - amount;
	}

	std::uint32_t decodeTrackLength(std::uint16_t encoded)
	{
		// The ordinary RCP v2 length is a multiple of four, so the low two bits
		// are unused.  Some Recomposer-compatible writers use them as bits 16/17.
		return static_cast<std::uint32_t>((encoded & ~0x03u) | ((encoded & 0x03u) << 16));
	}

	bool isRcpTrackFooter(const std::vector<std::uint8_t>& data, std::size_t offset)
	{
		return offset <= data.size() && data.size() - offset >= 4 && std::memcmp(data.data() + offset, "RCFW", 4) == 0;
	}

	bool parseRcpDocument(const std::vector<std::uint8_t>& data, RcpDocument& document, std::string& error)
	{
		if (data.size() < rcpHeaderSize)
		{
			error = "RCP header is truncated";
			return false;
		}

		const auto timeBase = static_cast<std::uint16_t>(data[0x1c0]) | static_cast<std::uint16_t>(data[0x1e7] << 8);
		document.timeBase = std::max(1, static_cast<int>(timeBase));
		document.tempoBpm = std::max(1, static_cast<int>(data[0x1c1]));
		document.beatNumerator = data[0x1c2] == 0 ? 4 : static_cast<int>(data[0x1c2]);
		document.beatDenominator = data[0x1c3];

		if (document.beatDenominator == 0 || document.beatDenominator > 32 ||
			(document.beatDenominator & (document.beatDenominator - 1)) != 0)
		{
			document.beatDenominator = 4;
		}

		document.userSysEx.clear();
		document.userSysEx.reserve(8);
		for (int i = 0; i < 8; ++i)
		{
			const auto offset = static_cast<std::size_t>(0x406 + i * 0x30 + 0x18);
			document.userSysEx.emplace_back(data.begin() + static_cast<std::ptrdiff_t>(offset),
											data.begin() + static_cast<std::ptrdiff_t>(offset + 0x18));
		}

		const auto declaredTrackCount = static_cast<std::size_t>(data[0x1e6]);
		document.hasExplicitTrackCount = declaredTrackCount != 0;
		// A zero track-count byte denotes the original 18-track RCP format;
		// nonzero files may explicitly contain up to 36 tracks.
		const auto trackLimit = std::min(
			maximumTrackCount, document.hasExplicitTrackCount ? declaredTrackCount : static_cast<std::size_t>(18));

		document.tracks.clear();
		std::size_t offset = rcpHeaderSize;
		for (std::size_t trackIndex = 0; trackIndex < trackLimit && offset + rcpTrackHeaderSize <= data.size();
			 ++trackIndex)
		{
			if (isRcpTrackFooter(data, offset))
				break;

			std::uint16_t encodedLength = 0;
			if (!readLittleEndian16(data, offset, encodedLength))
				break;

			const auto declaredLength = static_cast<std::size_t>(decodeTrackLength(encodedLength));
			if (declaredLength < rcpTrackHeaderSize)
				break;

			const auto availableLength = data.size() - offset;
			const auto trackLength = std::min(declaredLength, availableLength);
			if (trackLength < rcpTrackHeaderSize)
				break;

			RcpTrack track;
			const auto rawChannel = data[offset + 4];
			const auto channelOff = rawChannel == 0xff || (rawChannel & 0x80) != 0;
			const auto logicalChannel = static_cast<int>(rawChannel & 0x1f);
			track.port = static_cast<std::uint8_t>(logicalChannel >= 16 ? 1 : 0);
			track.channel = channelOff ? -1 : (logicalChannel & 0x0f);
			const auto rawTransposition = data[offset + 5];
			track.transposition = (rawTransposition & 0x80) != 0
				? 0
				: signedSevenBit(rawTransposition) + static_cast<int>(static_cast<std::int8_t>(data[0x1c5]));
			track.rawStartTick = data[offset + 6];
			track.startTick = static_cast<int>(static_cast<std::int8_t>(track.rawStartTick));
			track.muted = data[offset + 7] == 1;

			const auto eventBytes = trackLength - rcpTrackHeaderSize;
			const auto eventCount = std::min(maximumTrackEvents, eventBytes / rcpEventSize);
			track.events.reserve(eventCount);
			for (std::size_t eventIndex = 0; eventIndex < eventCount; ++eventIndex)
			{
				const auto eventOffset = offset + rcpTrackHeaderSize + eventIndex * rcpEventSize;
				track.events.push_back({data[eventOffset], data[eventOffset + 1], data[eventOffset + 2],
										data[eventOffset + 3], eventOffset - offset});

				if (track.events.back().command == 0xfe || track.events.back().command == 0xff)
					break;
			}

			document.tracks.push_back(std::move(track));

			if (trackLength < declaredLength)
				break;

			offset += trackLength;
		}

		// Old RCP files sometimes store ST+ as an unsigned byte.  The converter
		// used by winrcp/rcm2smf selects the unsigned interpretation only when a
		// legacy file contains a value that cannot reasonably be signed ST+.
		const bool signedStartTicks = document.hasExplicitTrackCount ||
			std::all_of(document.tracks.begin(), document.tracks.end(),
						[](const RcpTrack& track)
						{
							const auto signedValue = static_cast<int>(static_cast<std::int8_t>(track.rawStartTick));
							return signedValue >= -99 && signedValue <= 99;
						});
		if (!signedStartTicks)
		{
			for (auto& track : document.tracks)
				track.startTick = static_cast<int>(track.rawStartTick);
		}

		if (document.tracks.empty())
		{
			error = "RCP contains no usable tracks";
			return false;
		}

		return true;
	}

	bool addTimedEvent(std::vector<TimedEvent>& destination, std::int64_t tick, std::uint64_t& order,
					   std::vector<std::uint8_t> bytes, std::string& error)
	{
		if (bytes.empty())
			return true;

		if (destination.size() >= maximumOutputEvents)
		{
			error = "RCP expansion exceeded the event safety limit";
			return false;
		}

		destination.push_back({std::max<std::int64_t>(0, tick), order++, 0, std::move(bytes)});
		return true;
	}

	bool addShortEvent(std::vector<TimedEvent>& destination, std::int64_t tick, int channel, std::uint8_t status,
					   std::uint8_t data1, std::uint8_t data2, std::uint64_t& order, std::string& error)
	{
		if (channel < 0 || channel >= 16)
			return true;

		const auto message =
			std::vector<std::uint8_t>{static_cast<std::uint8_t>(status | static_cast<std::uint8_t>(channel)),
									  static_cast<std::uint8_t>(data1 & 0x7f), static_cast<std::uint8_t>(data2 & 0x7f)};
		return addTimedEvent(destination, tick, order, message, error);
	}

	bool addProgramChange(std::vector<TimedEvent>& destination, std::int64_t tick, int channel, std::uint8_t program,
						  std::uint64_t& order, std::string& error)
	{
		if (channel < 0 || channel >= 16)
			return true;

		const auto message = std::vector<std::uint8_t>{static_cast<std::uint8_t>(0xc0 | channel),
													   static_cast<std::uint8_t>(program & 0x7f)};
		return addTimedEvent(destination, tick, order, message, error);
	}

	std::vector<std::uint8_t> expandSysExTemplate(const std::vector<std::uint8_t>& body, std::uint8_t param1,
												  std::uint8_t param2, int channel)
	{
		std::vector<std::uint8_t> result;
		result.reserve(body.size() + 2);
		result.push_back(0xf0);

		int checksum = 0;
		for (const auto token : body)
		{
			int value = token;
			if (token == 0xf7)
				break;

			switch (token)
			{
			case 0x80:
				value = param1;
				break;
			case 0x81:
				value = param2;
				break;
			case 0x82:
				if (channel < 0)
					return {};
				value = channel;
				break;
			case 0x83:
				checksum = 0;
				continue;
			case 0x84:
				value = (0x100 - checksum) & 0x7f;
				break;
			case 0xf0:
				// Definitions are specified without F0, but accepting one
				// makes files exported by older tools harmless.
				continue;
			default:
				if ((token & 0x80) != 0)
					continue;
				break;
			}

			value &= 0x7f;
			result.push_back(static_cast<std::uint8_t>(value));
			checksum = (checksum + value) & 0x7f;
		}

		if (result.size() <= 1)
			return {};

		result.push_back(0xf7);
		return result;
	}

	bool addSysExTemplate(std::vector<TimedEvent>& destination, std::int64_t tick,
						  const std::vector<std::uint8_t>& body, std::uint8_t param1, std::uint8_t param2, int channel,
						  std::uint64_t& order, std::string& error)
	{
		if (channel < 0)
			return true;

		auto bytes = expandSysExTemplate(body, param1, param2, channel);
		if (bytes.size() <= 2)
			return true;

		return addTimedEvent(destination, tick, order, std::move(bytes), error);
	}

	std::vector<std::uint8_t> makeChannelSysEx(std::uint8_t command, int channel, std::uint8_t param1,
											   std::uint8_t param2)
	{
		const auto deviceChannel = static_cast<std::uint8_t>(0x10 + channel);
		std::vector<std::uint8_t> bytes{0xf0, 0x43, deviceChannel};

		switch (command)
		{
		case 0xc0:
			bytes.push_back(0x08);
			break;
		case 0xc1:
			bytes.push_back(0x00);
			break;
		case 0xc2:
			bytes.push_back(0x04);
			break;
		case 0xc3:
			bytes.push_back(0x11);
			break;
		case 0xc5:
			bytes.push_back(0x15);
			break;
		case 0xc7:
			bytes.push_back(0x12);
			break;
		case 0xc8:
			bytes.push_back(0x13);
			break;
		case 0xc9:
			bytes.push_back(0x10);
			break;
		case 0xca:
			bytes.insert(bytes.end(), {0x10, 0x7b});
			break;
		case 0xcb:
			bytes.insert(bytes.end(), {0x10, 0x7c});
			break;
		case 0xcc:
			bytes.push_back(0x1b);
			break;
		case 0xcd:
			bytes.push_back(0x18);
			break;
		case 0xce:
			bytes.push_back(0x19);
			break;
		case 0xcf:
			bytes.push_back(0x1a);
			break;
		default:
			return {};
		}

		bytes.push_back(param1 & 0x7f);
		bytes.push_back(param2 & 0x7f);
		bytes.push_back(0xf7);
		return bytes;
	}

	bool addChannelSysEx(std::vector<TimedEvent>& destination, std::int64_t tick, std::uint8_t command, int channel,
						 std::uint8_t param1, std::uint8_t param2, std::uint64_t& order, std::string& error)
	{
		if (channel < 0)
			return true;

		return addTimedEvent(destination, tick, order, makeChannelSysEx(command, channel, param1, param2), error);
	}

	bool addRolandParameter(std::vector<TimedEvent>& destination, std::int64_t tick, int channel, std::uint8_t device,
							std::uint8_t model, std::uint8_t baseHigh, std::uint8_t baseMiddle, std::uint8_t addressLow,
							std::uint8_t parameter, std::uint64_t& order, std::string& error)
	{
		if (channel < 0)
			return true;

		const auto checksum =
			static_cast<std::uint8_t>((0x100 - ((baseHigh + baseMiddle + addressLow + parameter) & 0x7f)) & 0x7f);
		const auto bytes = std::vector<std::uint8_t>{0xf0,
													 0x41,
													 static_cast<std::uint8_t>(device & 0x7f),
													 static_cast<std::uint8_t>(model & 0x7f),
													 0x12,
													 static_cast<std::uint8_t>(baseHigh & 0x7f),
													 static_cast<std::uint8_t>(baseMiddle & 0x7f),
													 static_cast<std::uint8_t>(addressLow & 0x7f),
													 static_cast<std::uint8_t>(parameter & 0x7f),
													 checksum,
													 0xf7};
		return addTimedEvent(destination, tick, order, bytes, error);
	}

	std::size_t skipContinuationEvents(const std::vector<RcpEvent>& events, std::size_t index)
	{
		while (index < events.size() && events[index].command == 0xf7)
			++index;
		return index;
	}

	bool repeatTargetIndex(const std::vector<RcpEvent>& events, const RcpEvent& event, std::size_t& target)
	{
		const auto targetOffset =
			static_cast<std::size_t>(event.param1 & 0xfc) | (static_cast<std::size_t>(event.param2) << 8);
		if (targetOffset < rcpTrackHeaderSize || (targetOffset - rcpTrackHeaderSize) % rcpEventSize != 0)
		{
			return false;
		}

		target = (targetOffset - rcpTrackHeaderSize) / rcpEventSize;
		return target < events.size();
	}

	// CVS.EXE's E7 graduation values are table indices, not milliseconds.
	const std::uint16_t tempoGraduationSteps[256] = {
		0,	 255, 225, 208, 195, 186, 178, 171, 165, 160, 156, 151, 148, 144, 141, 138, 135, 132, 130, 128, 125, 123,
		121, 119, 117, 116, 114, 112, 111, 109, 108, 106, 105, 104, 102, 101, 100, 99,	98,	 96,  95,  94,	93,	 92,
		91,	 90,  89,  88,	87,	 86,  86,  85,	84,	 83,  82,  81,	81,	 80,  79,  78,	78,	 77,  76,  76,	75,	 74,
		74,	 73,  72,  72,	71,	 70,  70,  69,	69,	 68,  67,  67,	66,	 66,  65,  65,	64,	 64,  63,  63,	62,	 62,
		61,	 61,  60,  60,	59,	 59,  58,  58,	57,	 57,  56,  56,	56,	 55,  55,  54,	54,	 53,  53,  53,	52,	 52,
		51,	 51,  51,  50,	50,	 49,  49,  49,	48,	 48,  48,  47,	47,	 47,  46,  46,	45,	 45,  45,  44,	44,	 44,
		43,	 43,  43,  42,	42,	 42,  42,  41,	41,	 41,  40,  40,	40,	 39,  39,  39,	38,	 38,  38,  38,	37,	 37,
		37,	 36,  36,  36,	36,	 35,  35,  35,	35,	 34,  34,  34,	33,	 33,  33,  33,	32,	 32,  32,  32,	31,	 31,
		31,	 31,  30,  30,	30,	 30,  29,  29,	29,	 29,  29,  28,	28,	 28,  28,  27,	27,	 27,  27,  26,	26,	 26,
		26,	 26,  25,  25,	25,	 25,  25,  24,	24,	 24,  24,  23,	23,	 23,  23,  23,	22,	 22,  22,  22,	22,	 21,
		21,	 21,  21,  21,	20,	 20,  20,  20,	20,	 20,  19,  19,	19,	 19,  19,  18,	18,	 18,  18,  18,	17,	 17,
		17,	 17,  17,  17,	16,	 16,  16,  16,	16,	 16,  15,  15,	15,	 15};

	int tempoGraduationTicks(int gradation, int timeBase)
	{
		if (gradation <= 0)
			return 0;

		const auto steps = tempoGraduationSteps[static_cast<std::size_t>(gradation & 0xff)];
		return std::max(1, static_cast<int>(std::lround(steps * timeBase / 48.0)));
	}

	class TrackExpander
	{
	public:
		TrackExpander(const RcpDocument& _document, const RcpTrack& _track, std::vector<TimedEvent>& _output,
					  std::vector<TempoModifier>& _tempo, std::uint64_t& _order, std::int64_t& _maximumTick,
					  std::string& _error) :
			m_document(_document), m_track(_track), m_output(_output), m_tempo(_tempo), m_order(_order),
			m_maximumTick(_maximumTick), m_error(_error), m_channel(_track.channel)
		{
		}

		bool run()
		{
			if (m_track.events.empty())
				return true;

			std::size_t index = 0;
			std::vector<LoopFrame> loops;
			std::int64_t currentTick = m_track.startTick;
			std::size_t steps = 0;

			while (index < m_track.events.size())
			{
				if (++steps > maximumInterpreterSteps)
				{
					m_error = "RCP track interpreter exceeded the safety limit";
					return false;
				}

				if (!flushDueNotes(currentTick))
					return false;

				const auto& event = m_track.events[index];
				const auto command = event.command;

				if (command < 0x80)
				{
					if (!emitNote(currentTick, event))
						return false;

					if (!advance(currentTick, event.delay))
						return false;
					++index;
					continue;
				}

				switch (command)
				{
				case 0x90:
				case 0x91:
				case 0x92:
				case 0x93:
				case 0x94:
				case 0x95:
				case 0x96:
				case 0x97:
					if (!addSysExTemplate(m_output, currentTick, m_document.userSysEx[command - 0x90], event.param1,
										  event.param2, m_channel, m_order, m_error))
						return false;
					if (!advance(currentTick, event.delay))
						return false;
					++index;
					break;

				case 0x98:
					{
						std::vector<std::uint8_t> body{event.param1, event.param2};
						body.reserve(body.size() + 2 * (m_track.events.size() - index - 1));
						auto next = index + 1;
						while (next < m_track.events.size() && m_track.events[next].command == 0xf7)
						{
							body.push_back(m_track.events[next].param1);
							body.push_back(m_track.events[next].param2);
							++next;
						}

						if (!addSysExTemplate(m_output, currentTick, body, event.param1, event.param2, m_channel,
											  m_order, m_error))
							return false;
						if (!advance(currentTick, event.delay))
							return false;
						index = next;
						break;
					}

				case 0x99:
					if (!advance(currentTick, event.delay))
						return false;
					index = skipContinuationEvents(m_track.events, index + 1);
					break;

				case 0xc0:
				case 0xc1:
				case 0xc2:
				case 0xc3:
				case 0xc5:
				case 0xc6:
				case 0xc7:
				case 0xc8:
				case 0xc9:
				case 0xca:
				case 0xcb:
				case 0xcc:
				case 0xcd:
				case 0xce:
				case 0xcf:
					if (m_channel >= 0 && command == 0xc6)
					{
						if (!addTimedEvent(m_output, currentTick, m_order,
										   {0xf0, 0x43, 0x75, static_cast<std::uint8_t>(m_channel & 0x7f), 0x10,
											static_cast<std::uint8_t>(event.param1 & 0x7f),
											static_cast<std::uint8_t>(event.param2 & 0x7f), 0xf7},
										   m_error))
							return false;
					}
					else if (!addChannelSysEx(m_output, currentTick, command, m_channel, event.param1, event.param2,
											  m_order, m_error))
					{
						return false;
					}

					if (!advance(currentTick, event.delay))
						return false;
					++index;
					break;

				case 0xd0:
					m_yamahaBaseHigh = event.param1;
					m_yamahaBaseMiddle = event.param2;
					if (!advance(currentTick, event.delay))
						return false;
					++index;
					break;

				case 0xd1:
					m_yamahaDevice = event.param1;
					m_yamahaModel = event.param2;
					if (!advance(currentTick, event.delay))
						return false;
					++index;
					break;

				case 0xd2:
					if (m_channel >= 0 &&
						!addTimedEvent(m_output, currentTick, m_order,
									   {0xf0, 0x43, static_cast<std::uint8_t>(m_yamahaDevice & 0x7f),
										static_cast<std::uint8_t>(m_yamahaModel & 0x7f),
										static_cast<std::uint8_t>(m_yamahaBaseHigh & 0x7f),
										static_cast<std::uint8_t>(m_yamahaBaseMiddle & 0x7f),
										static_cast<std::uint8_t>(event.param1 & 0x7f),
										static_cast<std::uint8_t>(event.param2 & 0x7f), 0xf7},
									   m_error))
						return false;
					if (!advance(currentTick, event.delay))
						return false;
					++index;
					break;

				case 0xd3:
					if (m_channel >= 0 &&
						!addTimedEvent(m_output, currentTick, m_order,
									   {0xf0, 0x43, 0x10, 0x4c, static_cast<std::uint8_t>(m_yamahaBaseHigh & 0x7f),
										static_cast<std::uint8_t>(m_yamahaBaseMiddle & 0x7f),
										static_cast<std::uint8_t>(event.param1 & 0x7f),
										static_cast<std::uint8_t>(event.param2 & 0x7f), 0xf7},
									   m_error))
						return false;
					if (!advance(currentTick, event.delay))
						return false;
					++index;
					break;

				case 0xdc:
					if (m_channel >= 0 &&
						!addTimedEvent(m_output, currentTick, m_order,
									   {0xf0, 0x41, 0x32, static_cast<std::uint8_t>(m_channel & 0x7f),
										static_cast<std::uint8_t>(event.param1 & 0x7f),
										static_cast<std::uint8_t>(event.param2 & 0x7f), 0xf7},
									   m_error))
						return false;
					if (!advance(currentTick, event.delay))
						return false;
					++index;
					break;

				case 0xdd:
					m_rolandBaseHigh = event.param1;
					m_rolandBaseMiddle = event.param2;
					if (!advance(currentTick, event.delay))
						return false;
					++index;
					break;

				case 0xde:
					if (!addRolandParameter(m_output, currentTick, m_channel, m_rolandDevice, m_rolandModel,
											m_rolandBaseHigh, m_rolandBaseMiddle, event.param1, event.param2, m_order,
											m_error))
						return false;
					if (!advance(currentTick, event.delay))
						return false;
					++index;
					break;

				case 0xdf:
					m_rolandDevice = event.param1;
					m_rolandModel = event.param2;
					if (!advance(currentTick, event.delay))
						return false;
					++index;
					break;

				case 0xe1:
					if (!addShortEvent(m_output, currentTick, m_channel, 0xb0, 32, event.param2, m_order, m_error) ||
						!addProgramChange(m_output, currentTick, m_channel, event.param1, m_order, m_error))
						return false;
					if (!advance(currentTick, event.delay))
						return false;
					++index;
					break;

				case 0xe2:
					if (!addShortEvent(m_output, currentTick, m_channel, 0xb0, 0, event.param2, m_order, m_error) ||
						!addShortEvent(m_output, currentTick, m_channel, 0xb0, 32, 0, m_order, m_error) ||
						!addProgramChange(m_output, currentTick, m_channel, event.param1, m_order, m_error))
						return false;
					if (!advance(currentTick, event.delay))
						return false;
					++index;
					break;

				case 0xe5:
					if (!advance(currentTick, event.delay))
						return false;
					++index;
					break;

				case 0xe6:
					{
						const auto decoded = static_cast<int>(event.param1) - 1;
						m_channel = (event.param1 == 0 || (decoded & 0x80) != 0) ? -1 : decoded & 0x0f;
						if (!advance(currentTick, event.delay))
							return false;
						++index;
						break;
					}

				case 0xe7:
					m_tempo.push_back({std::max<std::int64_t>(0, currentTick), m_order++,
									   std::max(1, static_cast<int>(event.param1)), static_cast<int>(event.param2)});
					if (!advance(currentTick, event.delay))
						return false;
					++index;
					break;

				case 0xea:
					if (!addShortEvent(m_output, currentTick, m_channel, 0xd0, event.param1, 0, m_order, m_error))
						return false;
					if (!advance(currentTick, event.delay))
						return false;
					++index;
					break;

				case 0xeb:
					if (!addShortEvent(m_output, currentTick, m_channel, 0xb0, event.param1, event.param2, m_order,
									   m_error))
						return false;
					if (!advance(currentTick, event.delay))
						return false;
					++index;
					break;

				case 0xec:
					if (event.param1 < 0x80)
					{
						if (!addProgramChange(m_output, currentTick, m_channel, event.param1, m_order, m_error))
							return false;
					}
					if (!advance(currentTick, event.delay))
						return false;
					++index;
					break;

				case 0xed:
					if (!addShortEvent(m_output, currentTick, m_channel, 0xa0, event.param1, event.param2, m_order,
									   m_error))
						return false;
					if (!advance(currentTick, event.delay))
						return false;
					++index;
					break;

				case 0xee:
					if (!addShortEvent(m_output, currentTick, m_channel, 0xe0, event.param1, event.param2, m_order,
									   m_error))
						return false;
					if (!advance(currentTick, event.delay))
						return false;
					++index;
					break;

				case 0xf5:
					++index;
					break;

				case 0xf6:
					index = skipContinuationEvents(m_track.events, index + 1);
					break;

				case 0xf7:
					++index;
					break;

				case 0xf8:
					if (!handleLoopEnd(loops, index, event))
						return false;
					break;

				case 0xf9:
					if (loops.size() < 8)
						loops.push_back({index + 1, 0});
					++index;
					break;

				case 0xfc:
					if (!handleRepeatMeasure(index, event))
						return false;
					break;

				case 0xfd:
					if (m_repeatReturnIndex != noIndex)
					{
						index = m_repeatReturnIndex;
						m_repeatReturnIndex = noIndex;
					}
					else
					{
						++index;
					}
					break;

				case 0xfe:
					index = m_track.events.size();
					break;

				default:
					// Commands below F5 carry the normal delay field.  The
					// high command range is timing-free, like the documented
					// RCP meta commands.
					if (command < 0xf5 && !advance(currentTick, event.delay))
						return false;
					++index;
					break;
				}

				m_maximumTick = std::max(m_maximumTick, currentTick);
			}

			if (!flushAllNotes(currentTick))
				return false;
			m_maximumTick = std::max(m_maximumTick, currentTick);
			return true;
		}

	private:
		struct LoopFrame
		{
			std::size_t startIndex;
			int completedPasses;
		};

		static constexpr std::size_t noIndex = std::numeric_limits<std::size_t>::max();

		bool advance(std::int64_t& tick, std::uint8_t amount)
		{
			if (!canAdvance(tick, amount))
			{
				m_error = "RCP tick position overflow";
				return false;
			}

			tick += amount;
			return true;
		}

		bool flushDueNotes(std::int64_t tick)
		{
			for (auto& note : m_activeNotes)
			{
				if (note.active && note.offTick <= tick)
				{
					if (!addShortEvent(m_output, note.offTick, note.channel, 0x80,
									   static_cast<std::uint8_t>(&note - m_activeNotes), 0, m_order, m_error))
						return false;
					note.active = false;
				}
			}
			return true;
		}

		bool flushAllNotes(std::int64_t tick)
		{
			(void)tick;
			for (auto& note : m_activeNotes)
			{
				if (!note.active)
					continue;

				if (!addShortEvent(m_output, note.offTick, note.channel, 0x80,
								   static_cast<std::uint8_t>(&note - m_activeNotes), 0, m_order, m_error))
					return false;
				note.active = false;
			}
			return true;
		}

		bool emitNote(std::int64_t tick, const RcpEvent& event)
		{
			const auto noteValue = static_cast<int>(event.command) + m_track.transposition;
			if (noteValue < 0 || noteValue > 127 || event.param1 == 0 || event.param2 == 0)
				return true;

			auto& active = m_activeNotes[static_cast<std::size_t>(noteValue)];
			if (active.active)
			{
				if (!canAdvance(tick, event.param1))
				{
					m_error = "RCP note duration overflow";
					return false;
				}
				active.offTick = tick + event.param1;
				return true;
			}

			if (m_channel < 0)
				return true;

			if (!addShortEvent(m_output, tick, m_channel, 0x90, static_cast<std::uint8_t>(noteValue), event.param2,
							   m_order, m_error))
				return false;

			if (!canAdvance(tick, event.param1))
			{
				m_error = "RCP note duration overflow";
				return false;
			}

			active.active = true;
			active.offTick = tick + event.param1;
			active.channel = m_channel;
			return true;
		}

		bool handleLoopEnd(std::vector<LoopFrame>& loops, std::size_t& index, const RcpEvent& event)
		{
			if (loops.empty())
			{
				++index;
				return true;
			}

			auto& frame = loops.back();
			++frame.completedPasses;

			const auto requested = static_cast<int>(event.delay);
			const auto targetPasses = (requested == 0 || requested >= 0x7f) ? defaultLoopCount : requested;
			if (frame.completedPasses < targetPasses)
			{
				index = frame.startIndex;
			}
			else
			{
				loops.pop_back();
				++index;
			}

			return true;
		}

		bool handleRepeatMeasure(std::size_t& index, const RcpEvent& event)
		{
			if (m_repeatReturnIndex != noIndex)
			{
				index = m_repeatReturnIndex;
				m_repeatReturnIndex = noIndex;
				return true;
			}

			std::size_t target = noIndex;
			if (!repeatTargetIndex(m_track.events, event, target))
			{
				++index;
				return true;
			}

			// Follow FC -> FC chains before entering the repeated measure.  This
			// is how Recomposer represents a measure built from earlier measures.
			for (int guard = 0; guard < 64 && target < m_track.events.size() && m_track.events[target].command == 0xfc;
				 ++guard)
			{
				std::size_t chainedTarget = noIndex;
				if (!repeatTargetIndex(m_track.events, m_track.events[target], chainedTarget) ||
					chainedTarget == target)
				{
					++index;
					return true;
				}
				target = chainedTarget;
			}

			if (target >= m_track.events.size())
			{
				++index;
				return true;
			}

			m_repeatReturnIndex = index + 1;
			index = target;
			return true;
		}

		const RcpDocument& m_document;
		const RcpTrack& m_track;
		std::vector<TimedEvent>& m_output;
		std::vector<TempoModifier>& m_tempo;
		std::uint64_t& m_order;
		std::int64_t& m_maximumTick;
		std::string& m_error;

		int m_channel = -1;
		std::uint8_t m_yamahaDevice = 0x10;
		std::uint8_t m_yamahaModel = 0x4c;
		std::uint8_t m_yamahaBaseHigh = 0;
		std::uint8_t m_yamahaBaseMiddle = 0;
		std::uint8_t m_rolandDevice = 0x10;
		std::uint8_t m_rolandModel = 0x16;
		std::uint8_t m_rolandBaseHigh = 0;
		std::uint8_t m_rolandBaseMiddle = 0x10;
		std::size_t m_repeatReturnIndex = noIndex;
		ActiveNote m_activeNotes[128] = {};
	};

	double tempoAtTick(std::int64_t tick, std::int64_t startTick, double startBpm, std::int64_t endTick,
					   double targetBpm)
	{
		if (endTick <= startTick || tick >= endTick)
			return targetBpm;
		if (tick <= startTick)
			return startBpm;

		const auto ratio = static_cast<double>(tick - startTick) / static_cast<double>(endTick - startTick);
		return startBpm + (targetBpm - startBpm) * ratio;
	}

	std::vector<TempoPoint> resolveTempoModifiers(const RcpDocument& document, std::vector<TempoModifier> modifiers)
	{
		std::stable_sort(modifiers.begin(), modifiers.end(), [](const TempoModifier& a, const TempoModifier& b)
						 { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });

		std::vector<TempoPoint> points;
		double currentBpm = std::max(1, document.tempoBpm);
		std::int64_t gradStartTick = 0;
		std::int64_t gradEndTick = 0;
		double gradStartBpm = currentBpm;
		double gradTargetBpm = currentBpm;
		std::int64_t generatedThrough = 0;

		const auto addGraduationUntil = [&](std::int64_t limit)
		{
			if (gradEndTick <= gradStartTick)
				return;

			const auto end = std::min(limit, gradEndTick);
			for (auto tick = generatedThrough + 1; tick <= end; ++tick)
			{
				points.push_back(
					{tick, 0,
					 std::max(1.0, tempoAtTick(tick, gradStartTick, gradStartBpm, gradEndTick, gradTargetBpm))});
			}
			generatedThrough = std::max(generatedThrough, end);
		};

		for (const auto& modifier : modifiers)
		{
			const auto tick = std::max<std::int64_t>(0, modifier.tick);
			addGraduationUntil(tick);

			const auto currentAtCommand = tempoAtTick(tick, gradStartTick, gradStartBpm, gradEndTick, gradTargetBpm);
			const auto target = std::max(1.0, document.tempoBpm * (std::max(1, modifier.ratio) / 64.0));
			const auto duration = tempoGraduationTicks(modifier.gradation, document.timeBase);

			if (duration <= 0)
			{
				points.push_back({tick, modifier.order, target});
				currentBpm = target;
				gradStartTick = tick;
				gradEndTick = tick;
				gradStartBpm = target;
				gradTargetBpm = target;
				generatedThrough = std::max(generatedThrough, tick);
			}
			else
			{
				points.push_back({tick, modifier.order, currentAtCommand});
				currentBpm = target;
				gradStartTick = tick;
				gradEndTick = tick + duration;
				gradStartBpm = currentAtCommand;
				gradTargetBpm = target;
				generatedThrough = tick;
			}
		}

		addGraduationUntil(gradEndTick);

		std::stable_sort(points.begin(), points.end(), [](const TempoPoint& a, const TempoPoint& b)
						 { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });

		std::vector<TempoPoint> unique;
		for (const auto& point : points)
		{
			if (!unique.empty() && unique.back().tick == point.tick)
				unique.back() = point;
			else
				unique.push_back(point);
		}
		return unique;
	}

	class TempoTimeline
	{
	public:
		TempoTimeline(const RcpDocument& document, const std::vector<TempoModifier>& modifiers) :
			m_timeBase(std::max(1, document.timeBase)), m_initialBpm(std::max(1, document.tempoBpm))
		{
			const auto points = resolveTempoModifiers(document, modifiers);
			double seconds = 0.0;
			std::int64_t startTick = 0;
			double bpm = m_initialBpm;
			m_segments.push_back({startTick, seconds, bpm});

			for (const auto& point : points)
			{
				if (point.tick < startTick)
					continue;

				seconds += static_cast<double>(point.tick - startTick) * 60.0 / (bpm * m_timeBase);
				startTick = point.tick;
				bpm = std::max(1.0, point.bpm);
				m_segments.push_back({startTick, seconds, bpm});
			}
		}

		double secondsAt(std::int64_t tick) const
		{
			if (tick <= 0)
				return 0.0;

			const auto it =
				std::upper_bound(m_segments.begin(), m_segments.end(), tick,
								 [](std::int64_t value, const Segment& segment) { return value < segment.startTick; });
			const auto& segment = it == m_segments.begin() ? m_segments.front() : *(it - 1);
			return segment.startSeconds +
				static_cast<double>(tick - segment.startTick) * 60.0 / (segment.bpm * m_timeBase);
		}

		double finalBpmAt(std::int64_t tick) const
		{
			if (tick <= 0)
				return m_initialBpm;

			const auto it =
				std::upper_bound(m_segments.begin(), m_segments.end(), tick,
								 [](std::int64_t value, const Segment& segment) { return value < segment.startTick; });
			return std::max(1.0, (it == m_segments.begin() ? m_segments.front() : *(it - 1)).bpm);
		}

	private:
		struct Segment
		{
			std::int64_t startTick;
			double startSeconds;
			double bpm;
		};

		int m_timeBase;
		double m_initialBpm;
		std::vector<Segment> m_segments;
	};
} // namespace

namespace rcpfile
{
	bool isRcpV2(const std::vector<std::uint8_t>& data) noexcept
	{
		const auto prefixLength = sizeof(rcpHeaderPrefix) - 1;
		return data.size() >= prefixLength && std::memcmp(data.data(), rcpHeaderPrefix, prefixLength) == 0;
	}

	bool loadRcpV2(const std::vector<std::uint8_t>& data, std::vector<sc88smf::Event>& destination, std::string& error)
	{
		destination.clear();
		error.clear();

		if (!isRcpV2(data))
		{
			error = "Not an RCP v2 file";
			return false;
		}

		RcpDocument document;
		if (!parseRcpDocument(data, document, error))
			return false;

		std::vector<TimedEvent> timedEvents;
		std::vector<TempoModifier> tempoModifiers;
		std::uint64_t order = 0;
		std::int64_t maximumTick = 0;

		for (std::size_t trackIndex = 0; trackIndex < document.tracks.size(); ++trackIndex)
		{
			const auto& track = document.tracks[trackIndex];
			if (track.muted)
				continue;

			std::vector<TimedEvent> trackEvents;
			TrackExpander expander(document, track, trackEvents, tempoModifiers, order, maximumTick, error);
			if (!expander.run())
				return false;

			// R36 stores A1-A16 as 0-15 and B1-B16 as 16-31 in each track header,
			// allowing the MIDI system to be selected independently per track.
			const auto port = track.port;
			for (auto& event : trackEvents)
			{
				timedEvents.push_back({event.tick, event.order, port, std::move(event.bytes)});
			}
		}

		if (timedEvents.empty())
		{
			error = "RCP contains no MIDI events";
			return false;
		}

		std::stable_sort(timedEvents.begin(), timedEvents.end(), [](const TimedEvent& a, const TimedEvent& b)
						 { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });

		const TempoTimeline timeline(document, tempoModifiers);
		for (auto& event : timedEvents)
		{
			if (!std::isfinite(timeline.secondsAt(event.tick)))
			{
				error = "RCP tempo map produced a non-finite time";
				destination.clear();
				return false;
			}

			destination.push_back({timeline.secondsAt(event.tick), std::move(event.bytes), event.port});
		}
		return true;
	}
} // namespace rcpfile
