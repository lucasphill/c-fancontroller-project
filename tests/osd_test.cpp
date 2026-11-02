#include "../src/osd.h"
#include <iostream>
#include <vector>
using namespace fanctl;
namespace {
void expect(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Memory {
    std::vector<DWORD> words;
    Memory():words((40+sizeof(osd_detail::Application)+3*4608+3)/4) {
        auto& h=header();h={0x52545353,0x2000e,sizeof(osd_detail::Application),40,1,4608,40+sizeof(osd_detail::Application),3,0,0};
    }
    osd_detail::Header& header(){return *reinterpret_cast<osd_detail::Header*>(words.data());}
    char* slot(int i){return reinterpret_cast<char*>(words.data())+header().osdOffset+static_cast<std::size_t>(i)*header().osdStride;}
    void* data(){return words.data();}std::size_t size(){return words.size()*sizeof(DWORD);}
};
void tests(){
    const auto defaults=OsdSettings{};expect(!defaults.enabled && defaults.metrics==255,"Unsafe initial settings");
    for(int corner=0;corner<4;++corner){const OsdSettings input{true,corner,85};const auto output=decodeOsdSettings(encodeOsdSettings(input));expect(output.enabled && output.corner==corner && output.metrics==85,"Settings not preserved");}
    for(const auto* bad:{"FANCTL_OSD 2\n0 0 255","FANCTL_OSD 1\n2 0 255","FANCTL_OSD 1\n0 4 255","FANCTL_OSD 1\n0 0 -1","FANCTL_OSD 1\n0 0 255 extra"}){
        bool rejected=false;try{decodeOsdSettings(bad);}catch(const std::exception&){rejected=true;}expect(rejected,"Invalid settings accepted");
    }
    OsdValues values;values.cpuTemperature=60;values.gpuUsage=0;values.fps=60.1;
    const auto text=formatOsd(values,{});expect(text.find("60 C")!=std::string::npos && text.find("0 %")!=std::string::npos && text.find("indisponivel")!=std::string::npos,"Missing and zero readings conflated");
    OsdSettings none;none.metrics=0;expect(formatOsd(values,none).empty(),"Empty selection displayed");
    OsdSettings fpsOnly;fpsOnly.metrics=Fps;expect(formatOsd(values,fpsOnly).find("CPU")==std::string::npos,"Selection ignored");
    Memory memory;std::memcpy(memory.slot(0)+256,"MSIAfterburner",14);std::memcpy(memory.slot(1)+256,"OtherOwner",11);
    expect(osd_detail::update(memory.data(),memory.size(),text)==RtssStatus::Available,"Cannot reserve own slot");
    expect(std::strcmp(memory.slot(0)+256,"MSIAfterburner")==0 && std::strcmp(memory.slot(1)+256,"OtherOwner")==0,"Changed another OSD owner");
    expect(std::strcmp(memory.slot(2)+256,"FanController")==0,"Owner not recorded");
    expect(std::strcmp(memory.slot(2)+512,text.c_str())==0,"Text not published");
    const auto frame=memory.header().frame;memory.header().busy=1;
    expect(osd_detail::update(memory.data(),memory.size(),"blocked")==RtssStatus::Busy && memory.header().busy==1 && memory.header().frame==frame,"Busy lock violated");
    memory.header().busy=0;expect(osd_detail::update(memory.data(),memory.size(),{},true)==RtssStatus::Available && !memory.slot(2)[256],"Owned slot not released");
    expect(std::strcmp(memory.slot(1)+256,"OtherOwner")==0,"Release erased foreign slot");
    memory.header().osdCount=0xffffffff;expect(osd_detail::update(memory.data(),memory.size(),"bad")==RtssStatus::Incompatible,"Unchecked table bounds");
    Memory game;osd_detail::Application app{};app.processId=123;app.time0=1000;app.time1=2000;app.frames=60;app.frameTime=16667;
    std::memcpy(reinterpret_cast<char*>(game.data())+game.header().appOffset,&app,sizeof(app));
    const auto metrics=osd_detail::game(game.data(),game.size(),123,2100);expect(metrics.fps && *metrics.fps==60 && metrics.frameTimeMs && *metrics.frameTimeMs>16.6,"Wrong frame units");
    expect(!osd_detail::game(game.data(),game.size(),124,2100).fps,"Read unrelated application");
    expect(!osd_detail::game(game.data(),game.size(),123,6000).fps,"Stale game sample returned");
}
}
int main(){try{tests();std::cout<<"OSD ownership, bounds, metrics and settings checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
