#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/history_chart.h"
#include <iostream>

namespace {
using namespace fanctl;
HistorySample sample(HistoryTime time,int temperature,GpuSource source) {
    HistorySample result;result.time=time;result.gpuSource=source;
    result.fans[1]={temperature,10,5000};return result;
}
int connectingPixels(std::vector<HistorySample> samples,bool otherMetrics=true) {
    const auto first=samples.front(),last=samples.back();
    if (!otherMetrics) for (auto& value : samples) {value.fans[1].boost.reset();value.fans[1].rpm.reset();}
    HistoryView view;view.loading=false;view.from=first.time-60;view.until=last.time+60;
    view.samples=std::make_shared<const std::vector<HistorySample>>(std::move(samples));
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=592;
    info.bmiHeader.biHeight=-212;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* bits=nullptr;const auto bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    const auto dc=CreateCompatibleDC(nullptr);const auto before=SelectObject(dc,bitmap);
    HistoryChart chart(1);chart.update(view);RECT bounds{0,0,592,212};chart.draw(dc,bounds);GdiFlush();
    auto x=[&](HistoryTime time) {return 44+static_cast<int>((time-view.from)*454.0/(view.until-view.from));};
    int count=0;
    const auto* pixels=static_cast<const DWORD*>(bits);
    for (int px=x(first.time)+8;px<x(last.time)-8;++px) {
        const double fraction=static_cast<double>(px-x(first.time))/(x(last.time)-x(first.time));
        const double temperature=*first.fans[1].temperature+fraction*(*last.fans[1].temperature-*first.fans[1].temperature);
        const int y=165-static_cast<int>(temperature*103/125);
        for (int py=y-3;py<=y+3;++py)
            if ((pixels[py*592+px]&0xffffff)==0xd2412d) {++count;break;}
    }
    SelectObject(dc,before);DeleteDC(dc);DeleteObject(bitmap);return count;
}
}
int main() {
    using namespace fanctl;
    // Minimal replay of the reported chart: adjacent, valid Dell and NVML temperatures.
    auto first=sample(1792942755,30,GpuSource::Dell),second=sample(1792943057,46,GpuSource::Nvml);
    const int mixed=connectingPixels({first,second});
    std::cout<<"Different GPU sources: "<<mixed<<" connecting red columns (expected > 20)\n";
    auto sameSource=second;sameSource.gpuSource=GpuSource::Dell;
    const int same=connectingPixels({first,sameSource});
    const int isolated=connectingPixels({first,second},false);
    std::cout<<"Only source changed to match: "<<same<<" connecting red columns\n";
    std::cout<<"Boost and RPM removed: "<<isolated<<" connecting red columns\n";
    auto missing=sample((first.time+second.time)/2,0,GpuSource::Dell);missing.fans[1].temperature.reset();
    const int nullGap=connectingPixels({first,missing,second});
    auto delayed=second;delayed.time=first.time+600;
    const int timeGap=connectingPixels({first,delayed});
    std::cout<<"Absent temperature: "<<nullGap<<" connecting red columns (expected 0)\n";
    std::cout<<"Missed collection: "<<timeGap<<" connecting red columns (expected 0)\n";
    if (mixed<=20 || isolated<=20 || same<=mixed+20 || nullGap!=0 || timeGap!=0) return 1;
    std::cout<<"GPU temperature continuity, dashed source transitions and gaps passed\n";return 0;
}
