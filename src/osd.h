#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <cstdint>

namespace fanctl {
enum OsdMetric : std::uint32_t { CpuTemperature=1,CpuUsage=2,GpuTemperature=4,GpuUsage=8,CpuRpm=16,GpuRpm=32,Fps=64,FrameTime=128 };
struct OsdSettings { bool enabled=false; int corner=0; std::uint32_t metrics=255; };
struct OsdValues {
    std::optional<double> cpuTemperature,cpuUsage,gpuTemperature,gpuUsage,cpuRpm,gpuRpm,fps,frameTimeMs;
};
inline std::string encodeOsdSettings(const OsdSettings& s) {
    if (s.corner<0 || s.corner>3 || s.metrics>255) throw std::runtime_error("Configuracao OSD invalida.");
    return "FANCTL_OSD 1\n"+std::to_string(s.enabled?1:0)+" "+std::to_string(s.corner)+" "+std::to_string(s.metrics)+"\n";
}
inline OsdSettings decodeOsdSettings(const std::string& text) {
    std::istringstream input(text);std::string magic,extra;int version=0,enabled=-1,corner=-1;long long mask=-1;
    if (!(input>>magic>>version>>enabled>>corner>>mask) || magic!="FANCTL_OSD" || version!=1 ||
        enabled<0 || enabled>1 || corner<0 || corner>3 || mask<0 || mask>255 || (input>>extra))
        throw std::runtime_error("Configuracao OSD invalida.");
    return {enabled==1,corner,static_cast<std::uint32_t>(mask)};
}
inline std::filesystem::path osdSettingsPath() {
    wchar_t local[32768]{};const DWORD size=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
    if (!size || size>=32768) throw std::runtime_error("Pasta de configuracao OSD indisponivel.");
    return std::filesystem::path(local)/L"FanController"/L"osd-settings.txt";
}
inline OsdSettings loadOsdSettings() {
    const auto path=osdSettingsPath();std::error_code error;
    if (!std::filesystem::exists(path,error)) {if(error) throw std::runtime_error("Falha ao acessar configuracao OSD.");return {};}
    const auto size=std::filesystem::file_size(path,error);
    if (error || size>1024) throw std::runtime_error("Tamanho de configuracao OSD invalido.");
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Falha ao ler configuracao OSD.");
    return decodeOsdSettings(std::string(std::istreambuf_iterator<char>(file),{}));
}
inline void saveOsdSettings(const OsdSettings& settings) {
    const auto text=encodeOsdSettings(settings);const auto path=osdSettingsPath();
    std::filesystem::create_directories(path.parent_path());auto temporary=path;temporary+=L".tmp";
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Falha ao salvar configuracao OSD.");
    DWORD written=0;const bool ok=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr) && written==text.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if(!ok || !MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());throw std::runtime_error("Falha ao concluir configuracao OSD.");
    }
}
inline std::string osdNumber(std::optional<double> value,int precision=0) {
    if(!value || !std::isfinite(*value))return "indisponivel";
    std::ostringstream output;output.imbue(std::locale::classic());output<<std::fixed<<std::setprecision(precision)<<*value;return output.str();
}
inline std::string formatOsd(const OsdValues& v,const OsdSettings& s) {
    if(s.corner<0 || s.corner>3)return {};
    std::string body;
    const auto line=[&](const char* label,std::initializer_list<std::tuple<OsdMetric,std::optional<double>,const char*,int>> entries) {
        std::string row;
        for(const auto& entry:entries)if(s.metrics&std::get<0>(entry)) {
            if(!row.empty())row+=" | ";row+=osdNumber(std::get<1>(entry),std::get<3>(entry));row+=std::get<2>(entry);
        }
        if(!row.empty()){if(!body.empty())body+='\n';body+=label;body+=row;}
    };
    line("CPU  ",{{CpuTemperature,v.cpuTemperature," C",0},{CpuUsage,v.cpuUsage," %",0}});
    line("GPU  ",{{GpuTemperature,v.gpuTemperature," C",0},{GpuUsage,v.gpuUsage," %",0}});
    line("Jogo ",{{Fps,v.fps," FPS",1},{FrameTime,v.frameTimeMs," ms",1}});
    line("Fans ",{{CpuRpm,v.cpuRpm," RPM CPU",0},{GpuRpm,v.gpuRpm," RPM GPU",0}});
    if(body.empty())return {};
    // Sticky-layer positions from the RTSS shipped SDK example. Only this slot's text is changed.
    constexpr int positions[]={0,2,6,8};return "<P"+std::to_string(positions[s.corner])+"><L0>"+body;
}

enum class RtssStatus { Available,NotRunning,Incompatible,Busy,NoSlot };
inline const wchar_t* rtssStatusText(RtssStatus status) {
    switch(status) {
    case RtssStatus::Available:return L"OSD conectado ao RTSS";
    case RtssStatus::NotRunning:return L"OSD indisponível: abra o RTSS";
    case RtssStatus::Incompatible:return L"OSD indisponível: protocolo RTSS incompatível";
    case RtssStatus::Busy:return L"RTSS ocupado; nova tentativa na próxima atualização";
    default:return L"OSD indisponível: slots RTSS ocupados";
    }
}
struct GameMetrics {std::optional<double> fps,frameTimeMs;DWORD processId=0;};

namespace osd_detail {
// Minimal wire-layout prefixes derived from the installed RTSS SDK, not the full SDK header.
struct Header {DWORD signature,version,appStride,appOffset,appCount,osdStride,osdOffset,osdCount,frame;LONG busy;};
struct Application {DWORD processId;char name[260];DWORD flags,time0,time1,frames,frameTime;};
static_assert(sizeof(Header)==40 && offsetof(Application,time0)==268,"RTSS protocol layout changed");
inline bool range(std::size_t total,DWORD offset,DWORD stride,DWORD count,std::size_t minimum) {
    return offset>=sizeof(Header) && stride>=minimum && stride%4==0 && offset%4==0 &&
        offset<=total && count<=(total-offset)/stride;
}
inline bool valid(const Header& h,std::size_t size) {
    return h.signature==0x52545353 && h.version>=0x2000e && h.version<0x30000 &&
        range(size,h.osdOffset,h.osdStride,h.osdCount,4608) && range(size,h.appOffset,h.appStride,h.appCount,sizeof(Application));
}
inline bool owned(const char* owner) {return std::memcmp(owner,"FanController",14)==0;}
inline RtssStatus update(void* memory,std::size_t bytes,const std::string& text,bool release=false) {
    if(bytes<sizeof(Header))return RtssStatus::Incompatible;
    auto* header=static_cast<Header*>(memory);
    if(!valid(*header,bytes))return RtssStatus::Incompatible;
    if(InterlockedOr(&header->busy,1)&1)return RtssStatus::Busy;
    struct Unlock {LONG* busy;~Unlock(){InterlockedAnd(busy,~1L);}} unlock{&header->busy};
    if(!valid(*header,bytes))return RtssStatus::Incompatible;
    if(text.size()>=4096)return RtssStatus::Incompatible;
    for(int pass=0;pass<(release?1:2);++pass)for(DWORD i=1;i<header->osdCount;++i) {
        char* slot=static_cast<char*>(memory)+header->osdOffset+static_cast<std::size_t>(i)*header->osdStride;
        char* owner=slot+256;
        if(!owned(owner) && !(pass==1 && owner[0]=='\0'))continue;
        if(release) {std::memset(slot,0,4608);}
        else {std::memcpy(owner,"FanController",14);std::memset(slot,0,256);std::memcpy(slot+512,text.c_str(),text.size()+1);}
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&header->frame));return RtssStatus::Available;
    }
    return release?RtssStatus::Available:RtssStatus::NoSlot;
}
inline GameMetrics game(const void* memory,std::size_t bytes,DWORD foreground,DWORD now) {
    GameMetrics result;if(!foreground || bytes<sizeof(Header))return result;
    const auto& header=*static_cast<const Header*>(memory);if(!valid(header,bytes))return result;
    for(DWORD i=0;i<header.appCount;++i) {
        Application app{};std::memcpy(&app,static_cast<const char*>(memory)+header.appOffset+static_cast<std::size_t>(i)*header.appStride,sizeof(app));
        if(app.processId!=foreground)continue;
        const DWORD interval=app.time1-app.time0;
        if(!app.time0 || !app.time1 || now-app.time1>3000 || !interval || interval>10000)return result;
        result.processId=foreground;result.fps=1000.0*app.frames/interval;
        if(app.frameTime)result.frameTimeMs=app.frameTime/1000.0;
        return result;
    }
    return result;
}
class Mapping {
    HANDLE handle_=nullptr;
public:
    void* data=nullptr;std::size_t size=0;
    explicit Mapping(bool write) {
        const DWORD access=write?FILE_MAP_ALL_ACCESS:FILE_MAP_READ;
        handle_=OpenFileMappingW(access,FALSE,L"RTSSSharedMemoryV2");if(!handle_)return;
        data=MapViewOfFile(handle_,access,0,0,0);if(!data)return;
        MEMORY_BASIC_INFORMATION info{};if(VirtualQuery(data,&info,sizeof(info)))size=info.RegionSize;
    }
    ~Mapping(){if(data)UnmapViewOfFile(data);if(handle_)CloseHandle(handle_);}
    Mapping(const Mapping&)=delete;Mapping& operator=(const Mapping&)=delete;
};
}
class RtssOsd {
public:
    ~RtssOsd(){release();}
    RtssStatus publish(const std::string& text) {
        osd_detail::Mapping map(true);if(!map.data)return RtssStatus::NotRunning;
        return osd_detail::update(map.data,map.size,text,text.empty());
    }
    RtssStatus release() {
        osd_detail::Mapping map(true);return map.data?osd_detail::update(map.data,map.size,{},true):RtssStatus::NotRunning;
    }
    GameMetrics readGame() const {
        DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);osd_detail::Mapping map(false);
        return map.data?osd_detail::game(map.data,map.size,pid,GetTickCount()):GameMetrics{};
    }
};
inline bool openRtss() {
    // Use the installation registry rather than PATH/current directory executable lookup.
    for(const auto flag:{KEY_WOW64_32KEY,KEY_WOW64_64KEY}) {
        HKEY key=nullptr;
        if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\RTSS",0,KEY_READ|flag,&key)!=ERROR_SUCCESS)continue;
        wchar_t directory[32768]{};DWORD type=0,bytes=sizeof(directory);bool uninstall=false;
        auto status=RegQueryValueExW(key,L"InstallLocation",nullptr,&type,reinterpret_cast<BYTE*>(directory),&bytes);
        if(status!=ERROR_SUCCESS || !directory[0]) {
            bytes=sizeof(directory);uninstall=true;
            status=RegQueryValueExW(key,L"UninstallString",nullptr,&type,reinterpret_cast<BYTE*>(directory),&bytes);
        }
        RegCloseKey(key);
        if(status!=ERROR_SUCCESS || type!=REG_SZ || bytes<sizeof(wchar_t) || bytes>sizeof(directory) || directory[(bytes/sizeof(wchar_t))-1]!=L'\0')continue;
        std::filesystem::path folder(directory);
        if(uninstall) {
            const std::wstring command(directory);if(command.size()<3 || command.front()!=L'"' || command.back()!=L'"')continue;
            folder=std::filesystem::path(command.substr(1,command.size()-2)).parent_path();
        }
        if(!folder.is_absolute())continue;
        const auto exe=folder/L"RTSS.exe";
        if(!std::filesystem::is_regular_file(exe))continue;
        return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",exe.c_str(),nullptr,exe.parent_path().c_str(),SW_SHOWNORMAL))>32;
    }
    return false;
}
} // namespace fanctl
