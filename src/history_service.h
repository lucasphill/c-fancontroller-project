#pragma once
#include "thermal_history.h"
#include <shlobj.h>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace fanctl {
struct HistoryView {
    std::shared_ptr<const std::vector<HistorySample>> samples=std::make_shared<const std::vector<HistorySample>>();
    HistoryTime from=0,until=0;
    std::string error;
    bool loading=true;
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
    void offer(const std::optional<ControlFrame>& frame,GpuSource source) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!schedule_.due(GetTickCount64())) return;
        pending_=historySample(historyNow(),frame,source);wake_.notify_one();
    }
    void period(HistoryTime seconds) {
        {std::lock_guard<std::mutex> lock(mutex_);period_=seconds;reload_=true;view_.loading=true;}
        wake_.notify_one();
    }
    HistoryView view() {std::lock_guard<std::mutex> lock(mutex_);return view_;}
private:
    std::function<void()> changed_;
    std::function<std::filesystem::path()> path_;
    std::mutex mutex_;
    std::condition_variable wake_;
    HistorySchedule schedule_;
    std::optional<HistorySample> pending_;
    HistoryTime period_=86400;
    HistoryView view_;
    bool reload_=true,stop_=false;
    std::thread worker_;
    void work() {
        std::unique_ptr<HistoryStore> store;
        std::string saveError;
        while (true) {
            std::optional<HistorySample> sample;HistoryTime seconds;
            {std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock,[this] {return stop_ || pending_ || reload_;});
                if (stop_ && !pending_) return;
                sample=std::move(pending_);pending_.reset();seconds=period_;reload_=false;
            }
            HistoryView next;next.until=historyNow();next.from=next.until-seconds;next.loading=false;
            try {
                if (!store) store=std::make_unique<HistoryStore>(path_());
                if (sample) {store->append(*sample);saveError.clear();}
                else store->prune(next.until);
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
