#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/history_service.h"
#include <iostream>

namespace {
void require(bool condition,const char* message) {if (!condition) throw std::runtime_error(message);}
template<class Predicate> fanctl::HistoryView waitFor(fanctl::HistoryService& service,HANDLE event,Predicate predicate) {
    const auto until=GetTickCount64()+5000;
    while (true) {
        auto view=service.view();if (predicate(view)) return view;
        require(GetTickCount64()<until,"history worker timed out");WaitForSingleObject(event,100);
    }
}
}
int main() {
    using namespace fanctl;
    const auto directory=std::filesystem::temp_directory_path()/
        ("fanctl-history-worker-"+std::to_string(GetTickCount64()));
    const auto event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    try {
        require(event!=nullptr,"event unavailable");std::filesystem::create_directories(directory);
        const auto path=directory/"history.db";
        {
            HistoryService service([&] {SetEvent(event);},[&] {return path;});
            auto initial=waitFor(service,event,[](const HistoryView& view) {return !view.loading;});
            require(initial.error.empty() && initial.samples->empty(),"initial asynchronous load");
            ControlFrame frame{{0,{{50,1500,42},{51,1600,44}},{{1,63}}},{GpuSource::Nvml,51,{}}};
            service.offer(frame,GpuSource::Nvml);
            auto loaded=waitFor(service,event,[](const HistoryView& view) {return view.samples->size()==1;});
            require(loaded.samples->front().fans[0].boost==42,"background sample saved");
            // Frequent thermal frames must not produce additional rows.
            for (int i=0;i<100;++i) service.offer(frame,GpuSource::Nvml);
            service.period(3600);
            auto hour=waitFor(service,event,[](const HistoryView& view) {return !view.loading && view.until-view.from==3600;});
            require(hour.samples->size()==1,"period selection and cadence independent of input frequency");
        }
        {HistoryStore reopened(path);require(reopened.read(0,historyNow()).size()==1,"worker persisted on disk");}
        {
            HistoryService service([&] {SetEvent(event);},[&] {return directory/"missing"/"history.db";});
            auto failed=waitFor(service,event,[](const HistoryView& view) {return !view.loading;});
            require(!failed.error.empty(),"open failure visible without escaping thread");
            service.period(3600);
            auto retry=waitFor(service,event,[](const HistoryView& view) {return !view.loading;});
            require(!retry.error.empty(),"error remains visible after failed retry");
        }
        CloseHandle(event);std::filesystem::remove_all(directory);
        std::cout<<"History worker isolation and query tests passed\n";return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';if (event) CloseHandle(event);std::filesystem::remove_all(directory);return 1;
    }
}
