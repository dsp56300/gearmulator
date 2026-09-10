#include "nmmAutosave.h"

#include <algorithm>
#include <atomic>
#include <chrono>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace nmmJucePlugin
{
    namespace
    {
        constexpr uint32_t FileVersion=1;
        constexpr size_t FileHeaderSize=16;
        constexpr size_t MaxStateSize=128*1024*1024;
        constexpr std::chrono::milliseconds PollPeriod{100};
        constexpr std::chrono::milliseconds DebouncePeriod{500};
        std::atomic<unsigned> activeAutosaves{0};

        void appendU32(std::vector<uint8_t>& data,const uint32_t value)
        {
            for(unsigned i=0;i<4;++i) data.push_back(static_cast<uint8_t>(value>>(8*i)));
        }

        uint32_t readU32(const uint8_t* data)
        {
            uint32_t value=0;
            for(unsigned i=0;i<4;++i) value|=uint32_t(data[i])<<(8*i);
            return value;
        }
    }

    Autosave::Autosave(std::shared_ptr<PanelState> panel,juce::File configDirectory)
        : m_panel(std::move(panel))
        , m_target(configDirectory.getChildFile("nmm-recovery.state"))
        , m_active(configDirectory.getChildFile("nmm-recovery.active"))
        , m_temp(configDirectory.getChildFile("nmm-recovery.state.tmp_"+juce::String::toHexString(reinterpret_cast<uintptr_t>(this))))
    {
        // The marker distinguishes a previous crash from an ordinary clean
        // plugin launch. Host project state remains the authoritative source;
        // disk recovery is only consulted when the previous session did not
        // remove its active marker.
        const auto processId=std::to_string(static_cast<unsigned long long>(
#ifdef _WIN32
            _getpid()
#else
            getpid()
#endif
        ));
        const auto activeOwner=m_active.existsAsFile()?m_active.loadFileAsString().trim().toStdString():std::string{};
        m_recovered=!activeOwner.empty() && activeOwner!=processId && read(m_target,*m_panel);
        m_active.getParentDirectory().createDirectory();
        m_active.replaceWithText(processId);
        activeAutosaves.fetch_add(1,std::memory_order_acq_rel);
        m_initialRevision=m_panel->stateRevision.load(std::memory_order_acquire);
        m_thread=std::thread([this]{run();});
    }

    Autosave::~Autosave()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop=true;
        }
        m_changed.notify_one();
        if(m_thread.joinable()) m_thread.join();
        m_temp.deleteFile();
        if(activeAutosaves.fetch_sub(1,std::memory_order_acq_rel)==1)
            m_active.deleteFile();
    }

    uint32_t Autosave::checksum(const std::vector<uint8_t>& data)
    {
        // FNV-1a is used only to reject torn/corrupt recovery files. The
        // temporary-file replacement provides the atomicity guarantee.
        uint32_t hash=2166136261u;
        for(const auto byte:data)
        {
            hash^=byte;
            hash*=16777619u;
        }
        return hash;
    }

    bool Autosave::read(const juce::File& file,PanelState& panel)
    {
        if(!file.existsAsFile()) return false;
        juce::MemoryBlock block;
        if(!file.loadFileAsData(block) || block.getSize()<FileHeaderSize || block.getSize()>MaxStateSize+FileHeaderSize) return false;
        const auto* data=static_cast<const uint8_t*>(block.getData());
        if(!std::equal(data,data+4,"NMRK") || readU32(data+4)!=FileVersion) return false;
        std::vector<uint8_t> state(data+FileHeaderSize,data+block.getSize());
        const auto size=readU32(data+8);
        if(size!=state.size() || readU32(data+12)!=checksum(state)) return false;
        return applyPanelState(state,panel);
    }

    bool Autosave::write(const std::vector<uint8_t>& state) const
    {
        if(state.empty() || state.size()>MaxStateSize) return false;
        std::vector<uint8_t> file;
        file.reserve(FileHeaderSize+state.size());
        file.insert(file.end(),{'N','M','R','K'});
        appendU32(file,FileVersion);
        appendU32(file,static_cast<uint32_t>(state.size()));
        appendU32(file,checksum(state));
        file.insert(file.end(),state.begin(),state.end());
        m_target.getParentDirectory().createDirectory();
        if(!m_target.getParentDirectory().isDirectory()) return false;
        if(!m_temp.replaceWithData(file.data(),file.size())) return false;
        for(unsigned attempt=0;attempt<10;++attempt)
        {
            if(m_temp.moveFileTo(m_target)) return true;
            if(attempt<9) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        m_temp.deleteFile();
        return false;
    }

    bool Autosave::writeCurrent(uint64_t& revision)
    {
        std::vector<uint8_t> state;
        {
            std::lock_guard<std::mutex> lock(m_panel->mutex);
            state=encodePanelState(*m_panel);
            revision=m_panel->stateRevision.load(std::memory_order_acquire);
        }
        return write(state);
    }

    void Autosave::run()
    {
        // Capture the baseline before launching the thread. Otherwise an edit
        // made immediately after construction could be sampled as the initial
        // state and never reach the debounce writer.
        uint64_t observed=m_initialRevision;
        uint64_t saved=observed;
        auto lastChange=std::chrono::steady_clock::now();

        for(;;)
        {
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_changed.wait_for(lock,PollPeriod,[this]{return m_stop;});
                if(m_stop) break;
            }

            const auto current=m_panel->stateRevision.load(std::memory_order_acquire);
            if(current!=observed)
            {
                observed=current;
                lastChange=std::chrono::steady_clock::now();
            }
            if(observed==saved || std::chrono::steady_clock::now()-lastChange<DebouncePeriod) continue;

            uint64_t snapshotRevision=observed;
            if(writeCurrent(snapshotRevision)) saved=snapshotRevision;
        }

        // Persist the latest coherent state on orderly plugin destruction. A
        // crash leaves the last completed atomic snapshot in place.
        uint64_t snapshotRevision=observed;
        (void)writeCurrent(snapshotRevision);
    }
}
