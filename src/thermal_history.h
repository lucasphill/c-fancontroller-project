#pragma once
#include "control_session.h"
#include "../third_party/sqlite/sqlite3.h"
#include <array>
#include <chrono>
#include <filesystem>

namespace fanctl {
using HistoryTime = std::int64_t;
inline constexpr HistoryTime historyRetention = 90LL*24*60*60;
inline HistoryTime historyNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
struct HistoryFan {
    std::optional<int> temperature,boost,rpm;
};
struct HistorySample {
    HistoryTime time=0;
    std::array<HistoryFan,2> fans;
    GpuSource gpuSource=GpuSource::Dell;
};
inline HistorySample historySample(HistoryTime time,const std::optional<ControlFrame>& frame,GpuSource source) {
    HistorySample result;result.time=time;result.gpuSource=source;
    if (!frame) return result;
    result.gpuSource=frame->gpu.source;
    for (const auto& sensor : frame->snapshot.sensors)
        if (sensor.id==1 && sensor.temperature>0 && sensor.temperature<=125)
            result.fans[0].temperature=static_cast<int>(sensor.temperature);
    if (frame->gpu.temperature && *frame->gpu.temperature>0 && *frame->gpu.temperature<=125)
        result.fans[1].temperature=frame->gpu.temperature;
    for (const auto& fan : frame->snapshot.fans) {
        if (fan.id!=50 && fan.id!=51) continue;
        auto& target=result.fans[fan.id-50];
        if (fan.boost<=255) target.boost=static_cast<int>(fan.boost);
        if (fan.rpm<=100000) target.rpm=static_cast<int>(fan.rpm);
    }
    return result;
}
// Elapsed ticks include suspended time on Windows. Never backfill missed intervals.
class HistorySchedule {
public:
    bool due(std::uint64_t ticks) {
        if (started_ && ticks-last_<300000) return false;
        started_=true;last_=ticks;return true;
    }
private:
    bool started_=false;
    std::uint64_t last_=0;
};

class HistoryStore {
public:
    explicit HistoryStore(const std::filesystem::path& path) {
        const auto utf8=path.u8string();
        if (sqlite3_open_v2(utf8.c_str(),&db_,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr)!=SQLITE_OK) {
            const std::string error=db_ ? sqlite3_errmsg(db_) : "Memoria insuficiente";
            if (db_) sqlite3_close(db_);
            db_=nullptr;throw std::runtime_error(error);
        }
        try {
            sqlite3_busy_timeout(db_,500);
            Statement version(db_,"PRAGMA user_version");
            if (version.step()!=SQLITE_ROW || sqlite3_column_int(version.get(),0)>1)
                throw std::runtime_error("Versao do historico nao suportada.");
            execute("CREATE TABLE IF NOT EXISTS samples (id INTEGER PRIMARY KEY, time INTEGER NOT NULL, "
                "cpu_temperature INTEGER, cpu_boost INTEGER, cpu_rpm INTEGER, "
                "gpu_temperature INTEGER, gpu_boost INTEGER, gpu_rpm INTEGER, gpu_source INTEGER NOT NULL CHECK(gpu_source IN (0,1)))");
            execute("CREATE INDEX IF NOT EXISTS samples_time ON samples(time)");
            execute("PRAGMA user_version=1");
        } catch (...) {sqlite3_close(db_);db_=nullptr;throw;}
    }
    ~HistoryStore() {if (db_) sqlite3_close(db_);}
    HistoryStore(const HistoryStore&)=delete;
    HistoryStore& operator=(const HistoryStore&)=delete;
    void append(const HistorySample& sample) {
        execute("BEGIN IMMEDIATE");
        try {
            Statement insert(db_,"INSERT INTO samples(time,cpu_temperature,cpu_boost,cpu_rpm,gpu_temperature,gpu_boost,gpu_rpm,gpu_source) VALUES(?,?,?,?,?,?,?,?)");
            check(sqlite3_bind_int64(insert.get(),1,sample.time));
            int column=2;
            for (const auto& fan : sample.fans) for (const auto& value : {fan.temperature,fan.boost,fan.rpm}) {
                check(value ? sqlite3_bind_int(insert.get(),column,*value) : sqlite3_bind_null(insert.get(),column));++column;
            }
            check(sqlite3_bind_int(insert.get(),8,sample.gpuSource==GpuSource::Nvml ? 1 : 0));
            insert.step();
            prune(sample.time);
            execute("COMMIT");
        } catch (...) {sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    }
    void prune(HistoryTime now) {
        Statement statement(db_,"DELETE FROM samples WHERE time < ?");
        check(sqlite3_bind_int64(statement.get(),1,now-historyRetention));statement.step();
    }
    std::vector<HistorySample> read(HistoryTime from,HistoryTime until) {
        Statement statement(db_,"SELECT time,cpu_temperature,cpu_boost,cpu_rpm,gpu_temperature,gpu_boost,gpu_rpm,gpu_source FROM samples WHERE time>=? AND time<=? ORDER BY time,id");
        check(sqlite3_bind_int64(statement.get(),1,from));check(sqlite3_bind_int64(statement.get(),2,until));
        std::vector<HistorySample> result;
        while (statement.step()==SQLITE_ROW) {
            auto* row=statement.get();HistorySample sample;sample.time=sqlite3_column_int64(row,0);
            int column=1;
            for (auto& fan : sample.fans) for (auto* field : {&fan.temperature,&fan.boost,&fan.rpm}) {
                if (sqlite3_column_type(row,column)!=SQLITE_NULL) *field=sqlite3_column_int(row,column);
                ++column;
            }
            sample.gpuSource=sqlite3_column_int(row,7) ? GpuSource::Nvml : GpuSource::Dell;
            result.push_back(sample);
        }
        return result;
    }
private:
    sqlite3* db_=nullptr;
    class Statement {
    public:
        Statement(sqlite3* db,const char* sql) {
            if (sqlite3_prepare_v2(db,sql,-1,&statement_,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db));
        }
        ~Statement() {sqlite3_finalize(statement_);}
        sqlite3_stmt* get() const {return statement_;}
        int step() {
            const int result=sqlite3_step(statement_);
            if (result!=SQLITE_ROW && result!=SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(sqlite3_db_handle(statement_)));
            return result;
        }
    private:
        sqlite3_stmt* statement_=nullptr;
    };
    void check(int result) {if (result!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));}
    void execute(const char* sql) {check(sqlite3_exec(db_,sql,nullptr,nullptr,nullptr));}
};
} // namespace fanctl
