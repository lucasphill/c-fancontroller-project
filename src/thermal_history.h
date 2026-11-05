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
    std::optional<int> temperatureMin,temperatureMax,rpmMin,rpmMax;
};
struct HistorySample {
    HistoryTime time=0;
    std::array<HistoryFan,2> fans;
    GpuSource gpuSource=GpuSource::Dell;
    std::string configuration;
    std::string segment;
    int interval=2;
    HistoryTime end=0;
    int count=1;
};
struct HistoryEvent {HistoryTime time=0;std::string label,configuration;};
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
        if (started_ && ticks-last_<2000) return false;
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
            if (version.step()!=SQLITE_ROW || sqlite3_column_int(version.get(),0)>2)
                throw std::runtime_error("Versao do historico nao suportada.");
            const int schema=sqlite3_column_int(version.get(),0);
            execute("BEGIN IMMEDIATE");
            try {
            execute("CREATE TABLE IF NOT EXISTS samples (id INTEGER PRIMARY KEY, time INTEGER NOT NULL, "
                "cpu_temperature INTEGER, cpu_boost INTEGER, cpu_rpm INTEGER, "
                "gpu_temperature INTEGER, gpu_boost INTEGER, gpu_rpm INTEGER, gpu_source INTEGER NOT NULL CHECK(gpu_source IN (0,1)))");
            execute("CREATE INDEX IF NOT EXISTS samples_time ON samples(time)");
            if (schema<2) {
                execute("ALTER TABLE samples ADD COLUMN configuration TEXT NOT NULL DEFAULT ''");
                execute("ALTER TABLE samples ADD COLUMN segment TEXT NOT NULL DEFAULT 'legacy'");
                execute("ALTER TABLE samples ADD COLUMN cadence INTEGER NOT NULL DEFAULT 300");
            }
            execute("CREATE TABLE IF NOT EXISTS control_events(time INTEGER NOT NULL,label TEXT NOT NULL,configuration TEXT NOT NULL)");
            execute("CREATE INDEX IF NOT EXISTS events_time ON control_events(time)");
            execute("CREATE TABLE IF NOT EXISTS minute_samples(time INTEGER NOT NULL,until_time INTEGER NOT NULL,n INTEGER NOT NULL,"
                "cpu_temperature INTEGER,cpu_boost INTEGER,cpu_rpm INTEGER,gpu_temperature INTEGER,gpu_boost INTEGER,gpu_rpm INTEGER,"
                "gpu_source INTEGER NOT NULL,configuration TEXT NOT NULL,segment TEXT NOT NULL,"
                "cpu_min INTEGER,cpu_max INTEGER,cpu_rpm_min INTEGER,cpu_rpm_max INTEGER,"
                "gpu_min INTEGER,gpu_max INTEGER,gpu_rpm_min INTEGER,gpu_rpm_max INTEGER)");
            execute("CREATE INDEX IF NOT EXISTS minute_time ON minute_samples(time)");
            execute("PRAGMA user_version=2");execute("COMMIT");
            } catch (...) {sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
        } catch (...) {sqlite3_close(db_);db_=nullptr;throw;}
    }
    ~HistoryStore() {if (db_) sqlite3_close(db_);}
    HistoryStore(const HistoryStore&)=delete;
    HistoryStore& operator=(const HistoryStore&)=delete;
    void append(const HistorySample& sample) {
        execute("BEGIN IMMEDIATE");
        try {
            Statement insert(db_,"INSERT INTO samples(time,cpu_temperature,cpu_boost,cpu_rpm,gpu_temperature,gpu_boost,gpu_rpm,gpu_source,configuration,segment,cadence) VALUES(?,?,?,?,?,?,?,?,?,?,?)");
            check(sqlite3_bind_int64(insert.get(),1,sample.time));
            int column=2;
            for (const auto& fan : sample.fans) for (const auto& value : {fan.temperature,fan.boost,fan.rpm}) {
                check(value ? sqlite3_bind_int(insert.get(),column,*value) : sqlite3_bind_null(insert.get(),column));++column;
            }
            check(sqlite3_bind_int(insert.get(),8,sample.gpuSource==GpuSource::Nvml ? 1 : 0));
            check(sqlite3_bind_text(insert.get(),9,sample.configuration.c_str(),-1,SQLITE_TRANSIENT));
            check(sqlite3_bind_text(insert.get(),10,sample.segment.c_str(),-1,SQLITE_TRANSIENT));
            check(sqlite3_bind_int(insert.get(),11,sample.interval));
            insert.step();
            prune(sample.time);
            execute("COMMIT");
        } catch (...) {sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    }
    void prune(HistoryTime now) {
        const bool ownTransaction=sqlite3_get_autocommit(db_)!=0;
        if (ownTransaction) execute("BEGIN IMMEDIATE");
        try {compactAndPrune(now);if (ownTransaction) execute("COMMIT");}
        catch (...) {if (ownTransaction) sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    }
    void compactAndPrune(HistoryTime now) {
        Statement expired(db_,"DELETE FROM samples WHERE time < ?");
        check(sqlite3_bind_int64(expired.get(),1,now-historyRetention));expired.step();
        // Compact complete minutes only. Separate segments and null masks never bridge
        // process restarts, missing readings, source changes or configuration changes.
        const auto cutoff=(now-86400)/60*60;
        Statement compact(db_,"INSERT INTO minute_samples SELECT MIN(time),MAX(time),COUNT(*),"
            "CAST(ROUND(AVG(cpu_temperature)) AS INTEGER),CAST(ROUND(AVG(cpu_boost)) AS INTEGER),CAST(ROUND(AVG(cpu_rpm)) AS INTEGER),"
            "CAST(ROUND(AVG(gpu_temperature)) AS INTEGER),CAST(ROUND(AVG(gpu_boost)) AS INTEGER),CAST(ROUND(AVG(gpu_rpm)) AS INTEGER),"
            "gpu_source,configuration,segment,MIN(cpu_temperature),MAX(cpu_temperature),MIN(cpu_rpm),MAX(cpu_rpm),"
            "MIN(gpu_temperature),MAX(gpu_temperature),MIN(gpu_rpm),MAX(gpu_rpm) FROM samples WHERE time<? AND cadence=2 "
            "GROUP BY time/60,gpu_source,configuration,segment,cpu_temperature IS NULL,cpu_boost IS NULL,cpu_rpm IS NULL,"
            "gpu_temperature IS NULL,gpu_boost IS NULL,gpu_rpm IS NULL");
        check(sqlite3_bind_int64(compact.get(),1,cutoff));compact.step();
        Statement removeRaw(db_,"DELETE FROM samples WHERE time<? AND cadence=2");
        check(sqlite3_bind_int64(removeRaw.get(),1,cutoff));removeRaw.step();
        Statement statement(db_,"DELETE FROM samples WHERE time < ?");
        check(sqlite3_bind_int64(statement.get(),1,now-historyRetention));statement.step();
        for (const auto* sql : {"DELETE FROM minute_samples WHERE until_time < ?","DELETE FROM control_events WHERE time < ?"}) {
            Statement remove(db_,sql);check(sqlite3_bind_int64(remove.get(),1,now-historyRetention));remove.step();
        }
    }
    std::vector<HistorySample> read(HistoryTime from,HistoryTime until) {
        Statement statement(db_,"SELECT time,cpu_temperature,cpu_boost,cpu_rpm,gpu_temperature,gpu_boost,gpu_rpm,gpu_source,"
            "configuration,segment,cadence,time,1,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL FROM samples WHERE time>=? AND time<=? "
            "UNION ALL SELECT time,cpu_temperature,cpu_boost,cpu_rpm,gpu_temperature,gpu_boost,gpu_rpm,gpu_source,"
            "configuration,segment,60,until_time,n,cpu_min,cpu_max,cpu_rpm_min,cpu_rpm_max,gpu_min,gpu_max,gpu_rpm_min,gpu_rpm_max "
            "FROM minute_samples WHERE until_time>=? AND time<=? ORDER BY time");
        check(sqlite3_bind_int64(statement.get(),1,from));check(sqlite3_bind_int64(statement.get(),2,until));
        check(sqlite3_bind_int64(statement.get(),3,from));check(sqlite3_bind_int64(statement.get(),4,until));
        std::vector<HistorySample> result;
        while (statement.step()==SQLITE_ROW) {
            auto* row=statement.get();HistorySample sample;sample.time=sqlite3_column_int64(row,0);
            int column=1;
            for (auto& fan : sample.fans) for (auto* field : {&fan.temperature,&fan.boost,&fan.rpm}) {
                if (sqlite3_column_type(row,column)!=SQLITE_NULL) *field=sqlite3_column_int(row,column);
                ++column;
            }
            sample.gpuSource=sqlite3_column_int(row,7) ? GpuSource::Nvml : GpuSource::Dell;
            sample.configuration=reinterpret_cast<const char*>(sqlite3_column_text(row,8));
            sample.segment=reinterpret_cast<const char*>(sqlite3_column_text(row,9));
            sample.interval=sqlite3_column_int(row,10);sample.end=sqlite3_column_int64(row,11);sample.count=sqlite3_column_int(row,12);
            column=13;
            for (auto& fan : sample.fans) for (auto* field : {&fan.temperatureMin,&fan.temperatureMax,&fan.rpmMin,&fan.rpmMax}) {
                if (sqlite3_column_type(row,column)!=SQLITE_NULL) *field=sqlite3_column_int(row,column);
                ++column;
            }
            result.push_back(sample);
        }
        return result;
    }
    void event(const HistoryEvent& event) {
        Statement insert(db_,"INSERT INTO control_events(time,label,configuration) VALUES(?,?,?)");
        check(sqlite3_bind_int64(insert.get(),1,event.time));
        check(sqlite3_bind_text(insert.get(),2,event.label.c_str(),-1,SQLITE_TRANSIENT));
        check(sqlite3_bind_text(insert.get(),3,event.configuration.c_str(),-1,SQLITE_TRANSIENT));insert.step();
    }
    std::vector<HistoryEvent> events(HistoryTime from,HistoryTime until) {
        Statement query(db_,"SELECT time,label,configuration FROM control_events WHERE time>=? AND time<=? ORDER BY time,rowid");
        check(sqlite3_bind_int64(query.get(),1,from));check(sqlite3_bind_int64(query.get(),2,until));std::vector<HistoryEvent> result;
        while (query.step()==SQLITE_ROW) result.push_back({sqlite3_column_int64(query.get(),0),
            reinterpret_cast<const char*>(sqlite3_column_text(query.get(),1)),reinterpret_cast<const char*>(sqlite3_column_text(query.get(),2))});
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
