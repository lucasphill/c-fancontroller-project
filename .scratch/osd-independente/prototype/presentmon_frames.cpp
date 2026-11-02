// Throwaway integration proof using unmodified Intel PresentData v1.10.0 (MIT).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "presentmon_frames.h"
#include "PresentMonTraceConsumer.hpp"
#include "TraceSession.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace fanctl::prototype {
namespace { std::uint64_t qpcNow(){LARGE_INTEGER q{};QueryPerformanceCounter(&q);return q.QuadPart;} }
struct PresentMonFrames::Impl {
    mutable std::mutex mutex;
    FramesSnapshot value;
    std::unique_ptr<PMTraceConsumer> consumer;
    TraceSession session;
    std::thread traceThread,outputThread;
    std::atomic<bool> stopping{false};
    std::atomic<ULONG> traceReturn{ERROR_IO_PENDING};
    std::uint64_t newestQpc=0,frequency=1;
    DWORD target=0;

    void drain() {
        std::map<std::uint64_t,std::deque<std::uint64_t>> chains;
        std::uint64_t lostPresents=0,previousLossTotal=0,lastLossQpc=0;
        while(!stopping.load()) {
            std::vector<std::shared_ptr<PresentEvent>> events,lost;
            std::vector<ProcessEvent> processes;
            consumer->DequeuePresentEvents(events);
            consumer->DequeueLostPresentEvents(lost);
            consumer->DequeueProcessEvents(processes);
            lostPresents+=lost.size();
            for(const auto& event:events) if(event->ProcessId==target && !event->IsLost && event->PresentStartTime) {
                auto& times=chains[event->SwapChainAddress];
                if(!times.empty() && event->PresentStartTime<=times.back())continue;
                times.push_back(event->PresentStartTime);
                while(times.size()>4096 || (times.size()>1 && times.back()-times.front()>frequency))times.pop_front();
            }
            const auto now=qpcNow();
            auto selected=chains.end();
            for(auto iterator=chains.begin();iterator!=chains.end();) {
                const auto& times=iterator->second;
                if(times.empty() || (now>times.back() && now-times.back()>3*frequency)) {iterator=chains.erase(iterator);continue;}
                if(selected==chains.end() || times.size()>selected->second.size() ||
                    (times.size()==selected->second.size() && times.back()>selected->second.back()))selected=iterator;
                ++iterator;
            }
            ULONG eventsLost=0,buffersLost=0;
            session.CheckLostReports(&eventsLost,&buffersLost);
            const auto totalLost=static_cast<std::uint64_t>(eventsLost)+buffersLost+lostPresents;
            const bool newLoss=totalLost>previousLossTotal;
            if(newLoss) {lastLossQpc=now;chains.clear();selected=chains.end();}
            previousLossTotal=totalLost;
            {
                std::lock_guard<std::mutex> lock(mutex);
                value.fps.reset();value.frameTimeMs.reset();newestQpc=0;
                value.eventsLost=totalLost;
                value.status=L"ETW ativo · aguardando apresentações do jogo";
                if(traceReturn.load()!=ERROR_IO_PENDING) {
                    value.status=L"ProcessTrace terminou: código "+std::to_wstring(traceReturn.load());
                } else if(lastLossQpc && now-lastLossQpc<frequency) {
                    value.status=L"Eventos ETW perdidos: "+std::to_wstring(value.eventsLost)+L" · leitura de quadros inválida";
                } else if(selected!=chains.end() && selected->second.size()>=2) {
                    const auto& times=selected->second;
                    const auto interval=times.back()-times.front();
                    if(interval) {
                        value.fps=static_cast<double>(times.size()-1)*static_cast<double>(frequency)/static_cast<double>(interval);
                        value.frameTimeMs=1000.0/static_cast<double>(*value.fps);
                        newestQpc=times.back();
                        value.status=L"ETW · FPS médio em 1 s · intervalo médio entre apresentações";
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
};
PresentMonFrames::PresentMonFrames():impl_(std::make_unique<Impl>()){}
PresentMonFrames::~PresentMonFrames(){stop();}
void PresentMonFrames::start(DWORD pid) {
    stop();
    auto& self=*impl_;
    {std::lock_guard<std::mutex> lock(self.mutex);self.value={};self.newestQpc=0;}
    if(!pid)return;
    self.target=pid;
    self.consumer=std::make_unique<PMTraceConsumer>();
    self.consumer->mFilteredProcessIds=true;
    self.consumer->mTrackDisplay=false;
    self.consumer->mTrackGPU=false;
    self.consumer->mTrackInput=false;
    self.consumer->AddTrackedProcessForFiltering(pid);
    const auto name=L"FanController.Prototype."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(qpcNow());
    const auto status=self.session.Start(self.consumer.get(),nullptr,nullptr,name.c_str());
    if(status!=ERROR_SUCCESS) {
        std::lock_guard<std::mutex> lock(self.mutex);
        self.value.status=status==ERROR_ACCESS_DENIED?L"ETW indisponível: execute o protótipo como administrador":
            L"ETW indisponível: código "+std::to_wstring(status);
        self.consumer.reset();return;
    }
    self.frequency=static_cast<std::uint64_t>(self.session.mTimestampFrequency.QuadPart);
    if(!self.frequency)self.frequency=1;
    self.stopping=false;self.traceReturn=ERROR_IO_PENDING;
    const auto traceHandle=self.session.mTraceHandle;
    try {
        self.traceThread=std::thread([&self,traceHandle]{auto handle=traceHandle;self.traceReturn=ProcessTrace(&handle,1,nullptr,nullptr);});
        self.outputThread=std::thread([&self]{self.drain();});
    } catch(...) {stop();throw;}
}
void PresentMonFrames::stop() {
    auto& self=*impl_;self.stopping=true;
    if(self.outputThread.joinable())self.outputThread.join();
    if(self.session.mSessionHandle || self.session.mTraceHandle!=INVALID_PROCESSTRACE_HANDLE)self.session.Stop();
    if(self.traceThread.joinable())self.traceThread.join();
    self.consumer.reset();
    std::lock_guard<std::mutex> lock(self.mutex);self.value={};self.newestQpc=0;
}
FramesSnapshot PresentMonFrames::snapshot() const {
    const auto& self=*impl_;std::lock_guard<std::mutex> lock(self.mutex);auto result=self.value;
    const auto now=qpcNow();
    if(!self.newestQpc || now<self.newestQpc || now-self.newestQpc>3*self.frequency) {
        result.fps.reset();result.frameTimeMs.reset();
        if(self.newestQpc)result.status=L"ETW sem apresentações recentes · leitura indisponível";
    }
    return result;
}
}
