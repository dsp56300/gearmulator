#include "portMidiBridge.h"

#include <mutex>
#include <set>
#include <stdexcept>
#include "portmidi.h"

#include "synthLib/midiBufferParser.h"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace jucePlayer
{
    namespace
    {
        std::mutex g_namesMutex;
        std::multiset<std::pair<std::string, uint8_t>> g_names;
        std::string portSuffix(size_t port) { return " " + std::string(1, static_cast<char>('A' + port)); }

        class VirtualMidiPort
        {
        public:
            explicit VirtualMidiPort(const size_t _port, const std::string& _name)
            {
                std::lock_guard lock(s_mutex);
                if (s_users == 0 && Pm_Initialize() != pmNoError)
                    return;
                ++s_users;
                m_initialized = true;
                const auto inputName = _name + " MIDI IN" + portSuffix(_port);
                const auto outputName = _name + " MIDI OUT" + portSuffix(_port);
                m_virtualIn = Pm_CreateVirtualInput(inputName.c_str(), nullptr, nullptr);
                m_virtualOut = Pm_CreateVirtualOutput(outputName.c_str(), nullptr, nullptr);
                if (m_virtualIn >= 0 && Pm_OpenInput(&m_in, m_virtualIn, nullptr, 1024, nullptr, nullptr) != pmNoError)
                    m_in = nullptr;
                if (m_virtualOut >= 0 &&
                    Pm_OpenOutput(&m_out, m_virtualOut, nullptr, 1024, nullptr, nullptr, 0) != pmNoError)
                    m_out = nullptr;
            }

            ~VirtualMidiPort()
            {
                std::lock_guard lock(s_mutex);
                if (m_in)
                    Pm_Close(m_in);
                if (m_out)
                    Pm_Close(m_out);
                if (m_virtualIn >= 0)
                    Pm_DeleteVirtualDevice(m_virtualIn);
                if (m_virtualOut >= 0)
                    Pm_DeleteVirtualDevice(m_virtualOut);
                if (m_initialized && --s_users == 0)
                    Pm_Terminate();
            }

            VirtualMidiPort(const VirtualMidiPort&) = delete;
            VirtualMidiPort& operator=(const VirtualMidiPort&) = delete;

            template <typename Callback> void pollIn(Callback&& _callback)
            {
                if (!m_in)
                    return;
                while (Pm_Poll(m_in) > 0)
                {
                    PmEvent event;
                    if (Pm_Read(m_in, &event, 1) <= 0)
                        return;
                    emitMessageBytes(event.message, _callback);
                }
            }

            void send(synthLib::SMidiEvent& _event)
            {
                if (!m_out)
                    return;
                if (!_event.sysex.empty())
                {
                    // PortMidi scans until F7, so only pass a complete bounded message.
                    if (_event.sysex.front() == 0xf0 && _event.sysex.back() == 0xf7)
                        Pm_WriteSysEx(m_out, 0, _event.sysex.data());
                    return;
                }
                PmEvent event{};
                event.message = Pm_Message(_event.a, _event.b, _event.c);
                Pm_Write(m_out, &event, 1);
            }

        private:
            template <typename Cb> void emitMessageBytes(uint32_t _message, Cb&& _cb)
            {
                const uint8_t status = static_cast<uint8_t>(_message & 0xFF);

                if (status >= 0xF8)
                {
                    _cb(status);
                    return;
                }

                if (m_inboundInSysex || status == 0xF0)
                {
                    m_inboundInSysex = true;
                    for (int i = 0; i < 4; ++i)
                    {
                        const uint8_t b = static_cast<uint8_t>((_message >> (i * 8)) & 0xFF);
                        _cb(b);
                        if (b == 0xF7)
                        {
                            m_inboundInSysex = false;
                            return;
                        }
                    }
                    return;
                }

                if (status == 0)
                    return;

                const size_t len = messageLengthFromStatus(status);
                for (size_t i = 0; i < len; ++i)
                    _cb(static_cast<uint8_t>((_message >> (i * 8)) & 0xFF));
            }

            static size_t messageLengthFromStatus(uint8_t _status)
            {
                if (_status >= 0xF0)
                {
                    switch (_status)
                    {
                    case 0xF1:
                        return 2;
                    case 0xF2:
                        return 3;
                    case 0xF3:
                        return 2;
                    case 0xF6:
                        return 1;
                    case 0xF7:
                        return 1;
                    default:
                        return 1;
                    }
                }
                switch (_status & 0xF0)
                {
                case 0x80:
                case 0x90:
                case 0xA0:
                case 0xB0:
                case 0xE0:
                    return 3;
                case 0xC0:
                case 0xD0:
                    return 2;
                }
                return 1;
            }

            static inline std::mutex s_mutex;
            static inline unsigned s_users = 0;
            bool m_initialized = false;
            PortMidiStream* m_in = nullptr;
            PortMidiStream* m_out = nullptr;
            int m_virtualIn = pmNoDevice;
            int m_virtualOut = pmNoDevice;
            bool m_inboundInSysex = false;
        };

    } // namespace

    PortMidiBridge::PortMidiBridge(MidiInputCallback _midiInputCallback, std::string _name, const uint8_t _portCount) :
        juce::Thread(_name + " MIDI"), m_midiInputCallback(std::move(_midiInputCallback)), m_name(std::move(_name)),
        m_portCount(_portCount)
    {
        if (m_portCount == 0 || m_portCount > 16)
            throw std::invalid_argument("Virtual MIDI needs 1..16 ports");
        {
            std::lock_guard lock(g_namesMutex);
            g_names.insert({m_name, m_portCount});
        }
        startThread();
    }

    PortMidiBridge::~PortMidiBridge()
    {
        signalThreadShouldExit();
        notify();
        stopThread(2000);
        std::lock_guard lock(g_namesMutex);
        if (const auto found = g_names.find({m_name, m_portCount}); found != g_names.end())
            g_names.erase(found);
    }

    bool PortMidiBridge::isOwnVirtualPortName(const juce::String& _name)
    {
        std::lock_guard lock(g_namesMutex);
        for (const auto& [prefix, count] : g_names)
            for (uint8_t port = 0; port < count; ++port)
                if (_name == juce::String(prefix + " MIDI IN" + portSuffix(port)) ||
                    _name == juce::String(prefix + " MIDI OUT" + portSuffix(port)))
                    return true;

        return false;
    }

    void PortMidiBridge::setEnabled(const bool _enabled)
    {
        m_enabled.store(_enabled, std::memory_order_release);
        notify();
    }

    void PortMidiBridge::enqueueOutput(const synthLib::SMidiEvent& _event)
    {
        if (!isEnabled())
            return;

        if (m_output.full())
            return;
        m_output.push_back(_event);
    }

    void PortMidiBridge::run()
    {
        std::vector<std::unique_ptr<VirtualMidiPort>> ports(m_portCount);
        std::vector<synthLib::MidiBufferParser> parsers;
        parsers.reserve(m_portCount);
        std::vector<synthLib::SMidiEvent> events;
        bool portsOpen = false;

        while (!threadShouldExit())
        {
            const bool shouldOpen = isEnabled();
            if (shouldOpen != portsOpen)
            {
                for (auto& port : ports)
                    port.reset();
                portsOpen = false;

                if (shouldOpen)
                {
                    parsers.clear();
                    for (size_t i = 0; i < ports.size(); ++i)
                    {
                        parsers.emplace_back(synthLib::MidiEventSource::Physical);
                        ports[i] = std::make_unique<VirtualMidiPort>(i, m_name);
                    }
                    portsOpen = true;
                }
            }

            if (!portsOpen)
            {
                while (!m_output.empty())
                    (void)m_output.pop_front();
                wait(100);
                continue;
            }

            for (size_t port = 0; port < ports.size(); ++port)
            {
                ports[port]->pollIn([&parsers, port](const uint8_t _byte) { parsers[port].write(_byte); });
                events.clear();
                parsers[port].getEvents(events);
                for (auto& event : events)
                {
                    event.port = static_cast<uint8_t>(port);
                    m_midiInputCallback(std::move(event));
                }
            }

            while (!m_output.empty())
            {
                auto event = m_output.pop_front();
                if (event.port < ports.size())
                    ports[event.port]->send(event);
            }

            wait(1);
        }
    }
} // namespace jucePlayer
