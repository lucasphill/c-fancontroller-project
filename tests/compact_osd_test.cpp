#include "../src/compact_osd.h"
#include <iostream>
#include <limits>
using namespace fanctl;
namespace {
void expect(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
void settings() {
    const CompactOsdSettings defaults;
    expect(!defaults.enabled && defaults.metrics==255 && !defaults.migratedFromLegacy,"Unsafe new defaults");
    for(int corner=0;corner<4;++corner)for(const auto mask:{0u,1u,85u,255u}) {
        const auto old="FANCTL_OSD 1\n1 "+std::to_string(corner)+" "+std::to_string(mask)+"\n";
        const auto migrated=decodeCompactOsdSettings(old);
        expect(migrated.enabled && migrated.metrics==mask && migrated.migratedFromLegacy,"Lost enabled/mask during corner migration");
        const auto encoded=encodeCompactOsdSettings(migrated);
        expect(encoded=="FANCTL_OSD 2\n1 "+std::to_string(mask)+"\n","New format still stores a legacy corner");
        const auto saved=decodeCompactOsdSettings(encoded);
        expect(saved.enabled && saved.metrics==mask && !saved.migratedFromLegacy,"V2 roundtrip not canonical");
    }
    expect(!decodeCompactOsdSettings("FANCTL_OSD 1\n0 3 64").enabled,"Legacy off was turned on");
    expect(!decodeCompactOsdSettings("FANCTL_OSD 2\n0 0").enabled,"Saved off was turned on");
    for(const auto* bad:{"", "FANCTL_OSD 3\n1 255", "FANCTL_OSD 1\n1 4 255", "FANCTL_OSD 1\n1 -1 255",
        "FANCTL_OSD 2\n2 255", "FANCTL_OSD 2\n-1 255", "FANCTL_OSD 2\n1 -1", "FANCTL_OSD 2\n1 256",
        "FANCTL_OSD 2\n1 0 255", "FANCTL_OSD 1\n1 0 255 junk", "OTHER 2\n1 255", "FANCTL_OSD 2\n1"}) {
        bool rejected=false;try{decodeCompactOsdSettings(bad);}catch(const std::exception&){rejected=true;}
        expect(rejected,"Malformed preferences accepted");
    }
}
void selection() {
    const CompactOsdValues value{65,32,58,76,3200,3000,120,8.3333};
    const auto all=formatCompactOsd(value,{});
    expect(all==L"CPU 65°C · 32%  |  GPU 58°C · 76%  |  Fans 3200/3000 RPM  |  120 FPS · 8,3 ms","Approved line changed");
    expect(all.find(L'\n')==std::wstring::npos,"OSD must be one line");
    const auto only=[&](std::uint32_t mask){return formatCompactOsd(value,{true,mask});};
    expect(only(0).empty(),"No selected metrics should hide the strip");
    expect(only(CompactCpuUsage)==L"CPU 32%","Unselected CPU temperature still displayed");
    expect(only(CompactGpuTemperature)==L"GPU 58°C","Unselected GPU usage still displayed");
    expect(only(CompactCpuRpm)==L"Fan CPU 3200 RPM","Single CPU fan not identified");
    expect(only(CompactGpuRpm)==L"Fan GPU 3000 RPM","Single GPU fan not identified");
    expect(only(CompactFps)==L"120 FPS","FPS only selection ignored");
    expect(only(CompactFrameTime)==L"8,3 ms","Frame interval unit/locale incorrect");
    for(std::uint32_t mask=0;mask<=255;++mask) {
        const auto text=only(mask);
        expect(text.find(L'\n')==std::wstring::npos && text.find(L'<')==std::wstring::npos,"Legacy RTSS tags or extra lines leaked");
        expect(text.empty()==(mask==0),"A nonempty selection was lost");
    }
}
void unavailable() {
    CompactOsdValues value;value.cpuUsage=0;value.gpuUsage=100;value.cpuRpm=0;
    auto text=formatCompactOsd(value,{});
    expect(text.find(L"CPU —°C · 0%")!=std::wstring::npos,"Missing CPU and valid zero usage conflated");
    expect(text.find(L"Fans 0/— RPM")!=std::wstring::npos,"Stopped fan was shown as missing");
    expect(text.find(L"— FPS · — ms")!=std::wstring::npos,"Missing frame data invented");
    value.fps=std::numeric_limits<double>::infinity();value.frameTimeMs=std::numeric_limits<double>::quiet_NaN();
    text=formatCompactOsd(value,{true,CompactFps|CompactFrameTime});
    expect(text==L"— FPS · — ms","Nonfinite telemetry escaped formatter");
}
}
int main(){try{settings();selection();unavailable();std::cout<<"Compact OSD migration, metric selection and unavailable readings passed.\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
