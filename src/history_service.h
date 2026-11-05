#pragma once
#include "thermal_history.h"
#include <shlobj.h>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <deque>

namespace fanctl {
struct HistoryView {
    std::shared_ptr<const std::vector<HistorySample>> samples=std::make_shared<const std::vector<HistorySample>>();
    HistoryTime from=0,until=0;
    std::string error;
    bool loading=true;
    std::vector<HistoryEvent> events;
};
inline std::filesystem::path historyPath() {
    PWSTR raw=nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_CREATE,nullptr,&raw)))
        throw std::runtime_error("Pasta de dados do usuario indisponivel.");
    const std::filesystem::path folder=std::filesystem::path(raw)/L"FanController";
    CoTaskMemFree(raw);std::filesystem::create_directories(folder);
    return folder/L"history.db";
}
// The thermal worker only enqueues a snapshot. SQLite and queries run here.
class HistoryService {
public:
    explicit HistoryService(std::function<void()> changed,
        std::function<std::filesystem::path()> path=historyPath)
        : changed_(std::move(changed)),path_(std::move(path)),worker_([this] {work();}) {}
    ~HistoryService() {
        {std::lock_guard<std::mutex> lock(mutex_);stop_=true;}
        wake_.notify_one();worker_.join();
    }
    void offer(const std::optional<ControlFrame>& frame,GpuSource source,const std::string& configuration={}) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!schedule_.due(GetTickCount64())) return;
        auto sample=historySample(historyNow(),frame,source);sample.configuration=configuration;
        if (!lastTime_ || sample.time-lastTime_>4 || configuration!=lastConfiguration_ ||
            source!=lastSource_ || validMask(sample)!=lastMask_) ++segmentNumber_;
        sample.segment=session_+"/"+std::to_string(segmentNumber_);
        lastTime_=sample.time;lastSource_=source;lastConfiguration_=configuration;lastMask_=validMask(sample);
        if (pending_.size()>=120) pending_.pop_front();
        pending_.push_back(std::move(sample));wake_.notify_one();
    }
    void event(std::string label,std::string configuration) {
        {std::lock_guard<std::mutex> lock(mutex_);events_.push_back({historyNow(),std::move(label),std::move(configuration)});}
        wake_.notify_one();
    }
    void period(HistoryTime seconds) {
        {std::lock_guard<std::mutex> lock(mutex_);period_=seconds;requestedUntil_=0;reload_=true;view_.loading=true;}
        wake_.notify_one();
    }
    void range(HistoryTime from,HistoryTime until) {
        {std::lock_guard<std::mutex> lock(mutex_);period_=until-from;requestedUntil_=until;reload_=true;view_.loading=true;}
        wake_.notify_one();
    }
    HistoryView view() {std::lock_guard<std::mutex> lock(mutex_);return view_;}
private:
    std::function<void()> changed_;
    std::function<std::filesystem::path()> path_;
    std::mutex mutex_;
    std::condition_variable wake_;
    HistorySchedule schedule_;
    std::deque<HistorySample> pending_;
    std::deque<HistoryEvent> events_;
    HistoryTime lastTime_=0;
    std::string session_=std::to_string(historyNow())+"/"+std::to_string(GetTickCount64());
    std::string lastConfiguration_;
    GpuSource lastSource_=GpuSource::Dell;
    int lastMask_=0,segmentNumber_=0;
    static int validMask(const HistorySample& sample) {
        int mask=0,bit=1;
        for (const auto& fan : sample.fans) for (const auto& value : {fan.temperature,fan.boost,fan.rpm}) {if(value)mask|=bit;bit<<=1;}
        return mask;
    }
    HistoryTime period_=900;
    HistoryTime requestedUntil_=0,lastQuery_=0;
    HistoryView view_;
    bool reload_=true,stop_=false;
    std::thread worker_;
    void work() {
        std::unique_ptr<HistoryStore> store;
        std::string saveError;
        while (true) {
            std::deque<HistorySample> samples;std::deque<HistoryEvent> events;HistoryTime seconds,until;bool reload;
            {std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock,[this] {return stop_ || !pending_.empty() || !events_.empty() || reload_;});
                if (stop_ && pending_.empty() && events_.empty()) return;
                samples.swap(pending_);events.swap(events_);seconds=period_;until=requestedUntil_;reload=reload_;reload_=false;
            }
            HistoryView next;next.until=until?until:historyNow();next.from=next.until-seconds;next.loading=false;
            try {
                if (!store) store=std::make_unique<HistoryStore>(path_());
                if (!samples.empty()) {for (const auto& sample : samples) store->append(sample);saveError.clear();}
                else store->prune(next.until);
                for (const auto& event : events) store->event(event);
                if(!reload && events.empty() && (seconds>3600 || until) && historyNow()-lastQuery_<30)continue;
                lastQuery_=historyNow();
                next.events=store->events(next.from,next.until);
                next.samples=std::make_shared<const std::vector<HistorySample>>(store->read(next.from,next.until));
                next.error=saveError;
            } catch (const std::exception& error) {
                saveError=std::string("Historico indisponivel: ")+error.what();next.error=saveError;store.reset();
                std::lock_guard<std::mutex> lock(mutex_);next.samples=view_.samples;
                next.from=view_.from;next.until=view_.until;
            }
            {std::lock_guard<std::mutex> lock(mutex_);view_=std::move(next);if (reload_) view_.loading=true;}
            changed_();
        }
    }
};
} // namespace fanctl
