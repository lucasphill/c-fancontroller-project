#include "../src/thermal_history.h"
#include <iostream>

namespace {
void require(bool condition,const char* message) {if (!condition) throw std::runtime_error(message);}
template<class Function> void fails(Function action,const char* message) {
    bool failed=false;try {action();} catch (const std::exception&) {failed=true;}require(failed,message);
}
void execute(const std::filesystem::path& path,const char* sql) {
    sqlite3* db=nullptr;
    require(sqlite3_open(path.u8string().c_str(),&db)==SQLITE_OK,"open external database");
    const auto result=sqlite3_exec(db,sql,nullptr,nullptr,nullptr);sqlite3_close(db);
    require(result==SQLITE_OK,"execute external SQL");
}
}
int main() {
    using namespace fanctl;
    const auto directory=std::filesystem::temp_directory_path()/std::filesystem::u8path(
        "fanctl-historico-ç-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(directory);
        const auto path=directory/"history.db";
        HistorySchedule schedule;
        require(schedule.due(100),"first sample is immediate");
        require(!schedule.due(300099),"no sample before five minutes");
        require(schedule.due(300100),"sample at five minutes");
        require(schedule.due(9000000),"resume after suspension samples once");
        require(!schedule.due(9000001),"no catch-up burst");
        ControlFrame frame{{0,{{50,1200,45},{51,UINT32_MAX,UINT32_MAX}},{{1,65},{6,47}}},
            {GpuSource::Nvml,48,{}}};
        const auto converted=historySample(500,frame,GpuSource::Dell);
        require(converted.fans[0].temperature==65 && converted.fans[0].rpm==1200 && converted.fans[0].boost==45,"CPU telemetry conversion");
        require(converted.gpuSource==GpuSource::Nvml && converted.fans[1].temperature==48,"actual GPU source");
        require(!converted.fans[1].rpm && !converted.fans[1].boost,"failed metrics stay absent");
        require(!historySample(500,std::nullopt,GpuSource::Dell).fans[0].temperature,"missing frame never reuses readings");
        frame.snapshot.sensors={{1,255}};
        require(!historySample(500,frame,GpuSource::Nvml).fans[0].temperature,"invalid CPU temperature absent");
        const HistoryTime now=20000000,cutoff=now-historyRetention;
        {
            HistoryStore store(path);
            require(store.read(0,now).empty(),"empty new database");
            auto sample=converted;sample.time=cutoff-1;store.append(sample);
            sample.time=cutoff;store.append(sample);
            sample.time=now;sample.fans[0].rpm=0;store.append(sample);
            auto rows=store.read(0,now);
            require(rows.size()==2 && rows[0].time==cutoff,"90-day boundary retained, older removed");
            require(rows[1].fans[0].rpm==0 && !rows[1].fans[1].rpm,"zero distinct from absence");
            require(store.read(now,now).size()==1,"inclusive period boundaries");
            require(store.read(now+1,now+20).empty(),"period filtering");
        }
        {
            HistoryStore reopened(path);auto rows=reopened.read(0,now);
            require(rows.size()==2 && rows[0].gpuSource==GpuSource::Nvml,"persist and reopen source metadata");
            require(rows[0].fans[0].temperature==65 && !rows[0].fans[1].boost,"persist nullable metrics");
            // Force retention to fail after INSERT, proving the whole transaction rolls back.
            execute(path,"CREATE TRIGGER reject_delete BEFORE DELETE ON samples BEGIN SELECT RAISE(ABORT,'simulated storage failure'); END");
            auto sample=converted;sample.time=now+historyRetention+1;
            fails([&] {reopened.append(sample);},"write error surfaced");
            require(reopened.read(0,sample.time).size()==2,"failed transaction neither inserts nor prunes");
            execute(path,"DROP TRIGGER reject_delete");reopened.append(sample);
            require(reopened.read(0,sample.time).size()==1,"subsequent write recovers");
        }
        execute(path,"PRAGMA user_version=2");
        fails([&] {HistoryStore future(path);},"future schema must not be overwritten");
        fails([&] {HistoryStore inaccessible(directory/"missing"/"history.db");},"open failure surfaced");
        std::filesystem::remove_all(directory);
        std::cout<<"Thermal history tests passed\n";return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';std::filesystem::remove_all(directory);return 1;
    }
}
