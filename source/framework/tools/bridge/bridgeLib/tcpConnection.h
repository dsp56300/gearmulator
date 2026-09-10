#pragma once

#include <mutex>

#include "commandReader.h"
#include "commandWriter.h"

#include "networkLib/networkThread.h"
#include "networkLib/tcpStream.h"
#include "synthLib/deviceTypes.h"

#include "synthLib/midiTypes.h"

namespace synthLib
{
	struct SMidiEvent;
}

namespace networkLib
{
	class NetException;
}

namespace bridgeLib
{
	class AudioBuffers;

	class TcpConnection : CommandReader, protected networkLib::NetworkThread
	{
	public:
		TcpConnection(std::unique_ptr<networkLib::TcpStream>&& _stream);
		~TcpConnection() override;

		bool isValid() const { return m_stream && m_stream->isValid(); }

		void handleCommand(bridgeLib::Command _command, baseLib::BinaryStream& _in) override;
		void threadFunc() override;
		void threadLoopFunc() override;

		// Everything below shares one CommandWriter and one socket while the audio thread (audio +
		// MIDI), the message thread (device state, samplerate, dsp clock) and the network thread (rom
		// upload) all send. Each of these builds the command and writes it as one atomic operation.
		void send();
		void send(Command _command, const CommandStruct& _data);
		void send(Command _command);

		// STRUCTS
		virtual void handleData(const PluginDesc& _desc) {}
		virtual void handleData(const ServerInfo& _desc) {}
		virtual void handleData(const DeviceDesc& _desc) {}
		virtual void handleData(const DeviceCreateParams& _params) {}
		virtual void handleData(const SetSamplerate& _params) {}
		virtual void handleData(const SetDspClockPercent& _params) {}
		virtual void handleData(const SetUnknownCustomData& _params) {}
		virtual void handleData(const Error& _error) {}

		virtual void handleDeviceInfo(baseLib::BinaryStream& _in);

		// MIDI
		bool send(const synthLib::SMidiEvent& _ev);
		void handleMidi(baseLib::BinaryStream& _in);
		virtual void handleMidi(const synthLib::SMidiEvent& _e) {}

		// AUDIO
		void sendAudio(const float* const* _data, uint32_t _numChannels, uint32_t _numSamplesPerChannel);
		void sendAudio(AudioBuffers& _buffers, uint32_t _numChannels, uint32_t _numSamplesPerChannel);
		static uint32_t handleAudio(float* const* _output, baseLib::BinaryStream& _in);
		void handleAudio(AudioBuffers& _buffers, baseLib::BinaryStream& _in);
		virtual void handleAudio(baseLib::BinaryStream& _in);

		// DEVICE STATE
		virtual void handleRequestDeviceState(baseLib::BinaryStream& _in);
		virtual void handleRequestDeviceState(bridgeLib::RequestDeviceState& _requestDeviceState) {}
		virtual void handleDeviceState(baseLib::BinaryStream& _in);
		virtual void handleDeviceState(DeviceState& _state) {}
		const auto& getDeviceState() const { return m_deviceState; }
		auto& getDeviceState() { return m_deviceState; }

	protected:
		template<typename T>
		void handleStruct(baseLib::BinaryStream& _in)
		{
			T data;
			handleStruct(data, _in);
		}

		template<typename T>
		void handleStruct(T& _dst, baseLib::BinaryStream& _in)
		{
			_dst.read(_in);
			handleData(_dst);
		}

		virtual void handleException(const networkLib::NetException& _e) = 0;
		void close() const;
		void shutdown();

	private:
		// Call with m_sendMutex held.
		void sendUnlocked();

		std::unique_ptr<networkLib::TcpStream> m_stream;
		std::mutex m_sendMutex;
		CommandWriter m_writer;

		synthLib::SMidiEvent m_midiEvent;	// preallocated for receiver

		// Separate buffers per direction: sending runs on the audio thread, receiving on the network thread.
		std::vector<float> m_audioTransferBuffer;
		std::vector<float> m_audioReceiveBuffer;

		DeviceState m_deviceState;
	};
}
