// Explicit opt-in live integration probe. No fan setters or configuration writes.
#define NOMINMAX
#include "../src/monitoring.h"
#include "../src/osd.h"
#include "../src/windows_backend.h"
#include <iostream>

int main(int argc,char** argv) {
    const bool publish=argc==2 && std::string(argv[1])=="--publish";
    fanctl::ComRuntime com;
    std::unique_ptr<fanctl::DellReader> dell;
    try {dell=std::make_unique<fanctl::DellReader>();} catch(const std::exception& e) {std::cerr<<"Dell read-only unavailable: "<<e.what()<<'\n';}
    fanctl::MonitoringCache cache;
    fanctl::WindowsMonitoringUsage source;
    fanctl::RtssOsd osd;
    fanctl::OsdSettings settings;
    settings.enabled=true;
    bool connected=false;
    for(int i=0;i<(publish?60:5);++i) {
        const auto sample=source.read();
        const auto game=osd.readGame();
        const auto now=fanctl::monitoringNowMs();
        cache.submitUsage(sample,now);
        if(dell && i%2==0)try {cache.submitThermal({dell->read(),{fanctl::GpuSource::Dell,std::nullopt,{}}},now);}catch(const std::exception&){}
        const auto thermal=cache.snapshot(fanctl::monitoringNowMs());
        fanctl::OsdValues values;
        values.cpuUsage=sample.cpuUsage;values.gpuUsage=sample.gpuUsage;
        values.gpuTemperature=sample.gpuTemperature;
        values.cpuTemperature=thermal.cpuTemperature;values.cpuRpm=thermal.cpuRpm;values.gpuRpm=thermal.gpuRpm;
        values.fps=game.fps;values.frameTimeMs=game.frameTimeMs;
        const auto status=publish?osd.publish(fanctl::formatOsd(values,settings)):fanctl::RtssStatus::NotRunning;
        if(status==fanctl::RtssStatus::Available)connected=true;
        const auto number=[](std::optional<double> v){return v?std::to_string(*v):"null";};
        std::cout<<"{\"sample\":"<<i<<",\"cpu_usage\":"<<number(values.cpuUsage)
            <<",\"gpu_usage\":"<<number(values.gpuUsage)<<",\"gpu_temperature\":"<<number(values.gpuTemperature)
            <<",\"cpu_temperature\":"<<number(values.cpuTemperature)<<",\"cpu_rpm\":"<<number(values.cpuRpm)<<",\"gpu_rpm\":"<<number(values.gpuRpm)
            <<",\"fps\":"<<number(game.fps)<<",\"frame_ms\":"<<number(game.frameTimeMs)
            <<",\"foreground_game_pid\":"<<game.processId<<",\"rtss_status\":"<<static_cast<int>(status)<<"}"<<std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return !publish || connected?0:1;
}
