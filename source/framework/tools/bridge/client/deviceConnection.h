#pragma once

#include <atomic>
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
		void handleData(const bridgeLib::Error& _error) override;
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

		// The latest device state that was sent to or received from the server, for when it cannot be asked any more
		void getLastDeviceState(std::vector<uint8_t>& _state);

		void setSamplerate(float _samplerate);
		void setStateFromUnknownCustomData(const std::vector<uint8_t>& _state);
		void setDspClockPercent(uint32_t _percent);

	private:
		// _reply returns false if the message is not the reply, it then keeps waiting
		bool sendAwaitReply(const std::function<void()>& _send, const std::function<bool(baseLib::BinaryStream&)>& _reply, bridgeLib::Command _replyCommand);
		void onReply(bridgeLib::Command _command, baseLib::BinaryStream& _in);

		RemoteDevice& m_device;
		bridgeLib::DeviceDesc m_deviceDesc;

		// The audio thread waits in processAudio() for the output that the network thread delivers. Guards the
		// output side of m_audioBuffers, the input side belongs to the audio thread.
		std::mutex m_audioMutex;
		std::condition_variable m_audioCv;

		// sendAwaitReply() runs on the message thread and on whatever thread a host saves or loads state on. There
		// is one reply slot and the server answers in request order, so it is held across a request and its reply.
		std::mutex m_requestMutex;

		// Reply state of a pending sendAwaitReply, guarded by m_replyMutex. The network thread runs the
		// callback, the caller waits for it - so both the callback and the flag it sets have to live in the
		// object, not on the waiting thread's stack, which is gone once that wait times out.
		std::mutex m_replyMutex;
		std::condition_variable m_replyCv;
		std::function<bool(baseLib::BinaryStream&)> m_replyFunc;
		bridgeLib::Command m_replyCommand = bridgeLib::Command::Invalid;
		bool m_replyReceived = false;

		// Set by the network thread when it stops, so that nobody keeps waiting for audio or a reply
		std::atomic<bool> m_connectionLost{false};

		// Guards TcpConnection::getDeviceState(): the network thread receives into it, a request sends from it
		std::mutex m_deviceStateMutex;

		// Filled by the network thread, drained by the audio thread.
		std::mutex m_midiOutMutex;
		std::vector<synthLib::SMidiEvent> m_midiOut;

		bridgeLib::AudioBuffers m_audioBuffers;
	};
}
