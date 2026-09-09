#pragma once
#include "dsp56kBase/threadtools.h"
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#ifndef _WIN32
#include <pthread.h>
#endif

namespace nmm
{
    // One bounded DSP grant at a time. Ownership returns to the emulator worker
    // at wait(), including on exceptions. No allocation or logging per grant.
    class AudioWorker final
    {
    public:
        explicit AudioWorker(std::function<void()> execute): m_execute(std::move(execute))
        {
#ifdef _WIN32
            m_thread=std::thread([this]{run();});
#else
            // Match DSPThread: recursive JIT compilation needs more than the
            // macOS default 512 KB stack.
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setstacksize(&attr,8*1024*1024);
            const auto error=pthread_create(&m_thread,&attr,[](void* p)->void* {
                static_cast<AudioWorker*>(p)->run();return nullptr;
            },this);
            pthread_attr_destroy(&attr);
            if(error) throw std::runtime_error("Cannot create NMM audio DSP worker");
#endif
        }
        ~AudioWorker()
        {
            {std::lock_guard lock(m_mutex);m_stop=true;}
            m_work.notify_one();
#ifdef _WIN32
            m_thread.join();
#else
            pthread_join(m_thread,nullptr);
#endif
        }
        void start()
        {
            {std::lock_guard lock(m_mutex);m_pending=true;}
            m_work.notify_one();
        }
        void wait()
        {
            std::unique_lock lock(m_mutex);
            m_done.wait(lock,[this]{return !m_pending;});
            auto error=m_error;m_error=nullptr;
            if(error) std::rethrow_exception(error);
        }
    private:
        void run()
        {
            dsp56k::ThreadTools::setCurrentThreadName("NMM audio DSP");
            dsp56k::ThreadTools::setCurrentThreadPriority(dsp56k::ThreadPriority::Highest);
            std::unique_lock lock(m_mutex);
            for(;;)
            {
                m_work.wait(lock,[this]{return m_pending || m_stop;});
                if(m_stop && !m_pending) return;
                lock.unlock();
                std::exception_ptr error;
                try {m_execute();} catch(...) {error=std::current_exception();}
                lock.lock();m_error=error;m_pending=false;
                m_done.notify_one();
            }
        }
        std::function<void()> m_execute;
        std::mutex m_mutex;
        std::condition_variable m_work,m_done;
        bool m_pending=false,m_stop=false;
        std::exception_ptr m_error;
#ifdef _WIN32
        std::thread m_thread;
#else
        pthread_t m_thread{};
#endif
    };
}
