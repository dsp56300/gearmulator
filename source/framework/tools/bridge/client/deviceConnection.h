#pragma once

#include <mutex>
#include <condition_variable>

#include "bridgeLib/audioBuffers.h"
#include "bridgeLib/tcpConnection.h"

#include "synthLib/audioTypes.h"
#include "synthLib/deviceTypes.h"

namespace bridgeClient
{
	class RemoteDevice;

	class DeviceConnection : public bridgeLib::TcpConnection
	{
	public:
		DeviceConnection(RemoteDevice& _device, std::unique_ptr<networkLib::TcpStream>&& _stream);
		~DeviceConnection() override;

		void handleCommand(bridgeLib::Command _command, baseLib::BinaryStream& _in) override;

		void handleData(const bridgeLib::DeviceDesc& _desc) override;
		void handleDeviceInfo(baseLib::BinaryStream& _in) override;

		void handleException(const networkLib::NetException& _e) override;

		// INIT
		void sendDeviceCreateParams(bool _sendRom);

		// AUDIO
		bool processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, uint32_t _size, uint32_t _latency);
		void handleAudio(baseLib::BinaryStream& _in) override;

		// MIDI
		void handleMidi(const synthLib::SMidiEvent& _e) override;
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut);

		// DEVICE STATE
		bool getDeviceState(std::vector<uint8_t>& _state, synthLib::StateType _type);
		void handleDeviceState(baseLib::BinaryStream& _in) override;
		bool setDeviceState(const std::vector<uint8_t>& _state, synthLib::StateType _type);

		void setSamplerate(float _samplerate);
		void setStateFromUnknownCustomData(const std::vector<uint8_t>& _state);
		void setDspClockPercent(uint32_t _percent);

	private:
		bool sendAwaitReply(const std::function<void()>& _send, const std::function<void(baseLib::BinaryStream&)>& _reply, bridgeLib::Command _replyCommand);
		void onReply(bridgeLib::Command _command, baseLib::BinaryStream& _in);

		RemoteDevice& m_device;
		bridgeLib::DeviceDesc m_deviceDesc;

		std::mutex m_cvWaitMutex;
		std::condition_variable m_cvWait;

		// Reply state of a pending sendAwaitReply, guarded by m_cvWaitMutex. The network thread runs the
		// callback, the caller waits for it - so both the callback and the flag it sets have to live in the
		// object, not on the waiting thread's stack, which is gone once that wait times out.
		std::function<void(baseLib::BinaryStream&)> m_replyFunc;
		bridgeLib::Command m_replyCommand = bridgeLib::Command::Invalid;
		bool m_replyReceived = false;

		// Filled by the network thread, drained by the audio thread.
		std::mutex m_midiOutMutex;
		std::vector<synthLib::SMidiEvent> m_midiOut;

		bridgeLib::AudioBuffers m_audioBuffers;
	};
}
