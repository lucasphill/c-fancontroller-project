#include "presentmon_frames.h"
#include "frame_metrics.h"
// Match the unmodified upstream release library's PresentEvent layout.
#define PRESENTMON_ENABLE_DEBUG_TRACE 0
#include "PresentMonTraceConsumer.hpp"
#include "TraceSession.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>

namespace fanctl {
namespace {
std::uint64_t qpcNow(){LARGE_INTEGER value{};QueryPerformanceCounter(&value);return static_cast<std::uint64_t>(value.QuadPart);}
struct ProcessHandle {
    HANDLE handle=nullptr;
    ~ProcessHandle(){if(handle)CloseHandle(handle);}
};
class Capture {
public:
    explicit Capture(DWORD processId):target(processId) {
        process.handle=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,target);
        if(!process.handle)throw std::runtime_error("Não foi possível acompanhar o processo do jogo.");
        consumer=std::make_unique<PMTraceConsumer>();
        consumer->mFilteredProcessIds=true;consumer->mTrackDisplay=false;consumer->mTrackGPU=false;consumer->mTrackInput=false;
        consumer->AddTrackedProcessForFiltering(target);
    }
    ~Capture(){close();}
    Capture(const Capture&)=delete;
    Capture& operator=(const Capture&)=delete;
    ULONG open() {
        const auto name=L"FanController.Frames."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(qpcNow());
        const auto result=session.Start(consumer.get(),nullptr,nullptr,name.c_str());
        if(result!=ERROR_SUCCESS)return result;
        const auto hz=session.mTimestampFrequency.QuadPart;
        if(hz<=0){close();return ERROR_INVALID_DATA;}
        metrics=std::make_unique<FrameMetrics>(target,static_cast<std::uint64_t>(hz));
        const auto trace=session.mTraceHandle;
        traceThread=std::thread([this,trace]{auto handle=trace;traceResult=ProcessTrace(&handle,1,nullptr,nullptr);});
        return ERROR_SUCCESS;
    }
    void close() {
        if(session.mSessionHandle || session.mTraceHandle!=INVALID_PROCESSTRACE_HANDLE)session.Stop();
        if(traceThread.joinable())traceThread.join();
    }
    bool processExited()const{return WaitForSingleObject(process.handle,0)!=WAIT_TIMEOUT;}
    FramesSnapshot read() {
        newestQpc=0;
        FramesSnapshot result;result.processId=target;
        std::vector<std::shared_ptr<PresentEvent>> events,lost;
        std::vector<ProcessEvent> processes;
        consumer->DequeuePresentEvents(events);consumer->DequeueLostPresentEvents(lost);consumer->DequeueProcessEvents(processes);
        for(const auto& present:events)if(!present->IsLost)metrics->present(present->ProcessId,present->SwapChainAddress,present->PresentStartTime);
        const auto room=std::numeric_limits<std::uint64_t>::max()-lostPresents;
        lostPresents+=std::min<std::uint64_t>(room,static_cast<std::uint64_t>(lost.size()));
        ULONG eventsLost=0,buffersLost=0;
        const auto lossResult=session.CheckLostReports(&eventsLost,&buffersLost);
        if(lossResult!=ERROR_SUCCESS) {
            result.status=L"Falha ao verificar captura ETW: código "+std::to_wstring(lossResult);return result;
        }
        const auto now=qpcNow();
        const auto reports=static_cast<std::uint64_t>(eventsLost)+buffersLost;
        const auto total=lostPresents>std::numeric_limits<std::uint64_t>::max()-reports?std::numeric_limits<std::uint64_t>::max():lostPresents+reports;
        metrics->reportLoss(total,now);
        const auto value=metrics->snapshot(now);
        result.eventsLost=value.eventsLost;result.fps=value.fps;result.frameTimeMs=value.frameTimeMs;newestQpc=value.newestQpc;
        result.status=value.recovering?L"Perda de eventos ETW · aguardando intervalo íntegro":
            value.fps?L"ETW · FPS médio em 1 s · intervalo médio entre apresentações":L"ETW ativo · aguardando apresentações recentes do jogo";
        return result;
    }
    std::uint64_t frequency()const{return static_cast<std::uint64_t>(session.mTimestampFrequency.QuadPart);}
    DWORD target;
    std::atomic<ULONG> traceResult{ERROR_IO_PENDING};
    std::uint64_t newestQpc=0;
private:
    ProcessHandle process;
    std::unique_ptr<PMTraceConsumer> consumer;
    TraceSession session;
    std::thread traceThread;
    std::unique_ptr<FrameMetrics> metrics;
    std::uint64_t lostPresents=0;
};
}

struct PresentMonFrames::Impl {
    mutable std::mutex mutex;
    std::condition_variable wake;
    bool stopping=false;
    DWORD desiredTarget=0;
    std::uint64_t generation=0,newestQpc=0,frequency=1;
    FramesSnapshot value;
    std::thread worker;

    void publish(FramesSnapshot next,std::uint64_t expected,std::uint64_t newest=0,std::uint64_t hz=1) {
        std::lock_guard<std::mutex> lock(mutex);
        if(generation!=expected || stopping)return;
        value=std::move(next);newestQpc=newest;frequency=hz?hz:1;
    }
    void run() {
        std::unique_ptr<Capture> capture;
        std::uint64_t processed=std::numeric_limits<std::uint64_t>::max();
        while(true) {
            DWORD desired=0;std::uint64_t requested=0;
            {std::lock_guard<std::mutex> lock(mutex);if(stopping)break;desired=desiredTarget;requested=generation;}
            try {
                if(requested!=processed) {
                    capture.reset();processed=requested;
                    if(desired) {
                        auto next=std::make_unique<Capture>(desired);
                        const auto result=next->open();
                        if(result==ERROR_SUCCESS)capture=std::move(next);
                        else publish({{}, {},result==ERROR_ACCESS_DENIED?L"ETW indisponível: execute o FanController como administrador":L"ETW indisponível: código "+std::to_wstring(result),0,desired},requested);
                    }else publish({},requested);
                }
                if(capture) {
                    if(capture->processExited()) {
                        publish({{}, {},L"Jogo encerrado · aguardando nova instância",0,capture->target},requested);capture.reset();
                    }else if(capture->traceResult.load()!=ERROR_IO_PENDING) {
                        publish({{}, {},L"Captura ETW terminou: código "+std::to_wstring(capture->traceResult.load()),0,capture->target},requested);capture.reset();
                    }else {
                        auto next=capture->read();publish(std::move(next),requested,capture->newestQpc,capture->frequency());
                    }
                }
            }catch(const std::exception&) {
                capture.reset();publish({{}, {},L"Falha na medição de quadros · sensores e controle térmico preservados",0,desired},requested);
            }
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait_for(lock,std::chrono::milliseconds(100),[&]{return stopping || generation!=processed;});
        }
        capture.reset();
    }
};

PresentMonFrames::PresentMonFrames():impl_(std::make_unique<Impl>()) {
    impl_->worker=std::thread([self=impl_.get()]{self->run();});
}
PresentMonFrames::~PresentMonFrames(){stop();}
void PresentMonFrames::setTarget(DWORD processId) {
    {std::lock_guard<std::mutex> lock(impl_->mutex);if(impl_->stopping || impl_->desiredTarget==processId)return;
        impl_->desiredTarget=processId;++impl_->generation;impl_->newestQpc=0;
        impl_->value={{},{},processId?L"Inicializando medição de quadros":L"Medição de quadros desligada",0,processId};}
    impl_->wake.notify_one();
}
void PresentMonFrames::retry() {
    {std::lock_guard<std::mutex> lock(impl_->mutex);if(impl_->stopping || !impl_->desiredTarget)return;
        ++impl_->generation;impl_->newestQpc=0;impl_->value={{},{},L"Reiniciando medição de quadros",0,impl_->desiredTarget};}
    impl_->wake.notify_one();
}
FramesSnapshot PresentMonFrames::snapshot()const {
    std::lock_guard<std::mutex> lock(impl_->mutex);auto result=impl_->value;const auto now=qpcNow();
    if(impl_->newestQpc && (now<impl_->newestQpc || now-impl_->newestQpc>3*impl_->frequency)) {
        result.fps.reset();result.frameTimeMs.reset();result.status=L"Quadros sem atualização recente · leitura indisponível";
    }
    return result;
}
void PresentMonFrames::stop() {
    {std::lock_guard<std::mutex> lock(impl_->mutex);impl_->stopping=true;impl_->newestQpc=0;impl_->value={};}
    impl_->wake.notify_one();if(impl_->worker.joinable())impl_->worker.join();
}
}
