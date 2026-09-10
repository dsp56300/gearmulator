#pragma once

#include "nmmDevice.h"
#include <juce_core/juce_core.h>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace nmmJucePlugin
{
    // Best-effort crash-recovery copy of the coherent panel state. The worker
    // owns the live emulator; this class only observes PanelState and performs
    // all serialization and filesystem work on its own thread.
    class Autosave final
    {
    public:
        Autosave(std::shared_ptr<PanelState>,juce::File configDirectory);
        ~Autosave();

        bool recovered() const { return m_recovered; }

    private:
        void run();
        bool write(const std::vector<uint8_t>&) const;
        bool writeCurrent(uint64_t& revision);
        static bool read(const juce::File&,PanelState&);
        static uint32_t checksum(const std::vector<uint8_t>&);

        std::shared_ptr<PanelState> m_panel;
        juce::File m_target;
        juce::File m_active;
        juce::File m_temp;
        std::mutex m_mutex;
        std::condition_variable m_changed;
        bool m_stop=false;
        bool m_recovered=false;
        uint64_t m_initialRevision=0;
        std::thread m_thread;
    };
}
