// Opt-in live validation host for production OSD modules; read-only hardware access.
// Question: does this work visibly over Euro Truck without RTSS or code in the game?
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <filesystem>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include "../src/monitoring.h"
#include "../src/windows_backend.h"
#include "../src/overlay_window.h"
#include "../src/presentmon_frames.h"
#include "../src/compact_osd.h"

namespace {
using fanctl::OverlayWindow;
using fanctl::PresentMonFrames;
constexpr wchar_t prototypeClass[]=L"FanController.IndependentOverlay.Validation";
constexpr UINT_PTR updateTimer=1;
constexpr int closeId=101,toggleId=102;

std::wstring wide(const std::string& s) {
    if(s.empty())return {};
    const int count=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(count<=0)return L"Falha de diagnóstico";
    std::wstring text(static_cast<std::size_t>(count),L'\0');
    MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),text.data(),count);return text;
}
class Prototype {
public:
    explicit Prototype(HINSTANCE instance):instance_(instance){}
    ~Prototype() {
        stopping_=true;
        if(thermal_.joinable())thermal_.join();
        frames_.stop();monitor_.stop();
        if(font_)DeleteObject(font_);
        if(background_)DeleteObject(background_);
    }
    bool create(int show,int duration,bool record) {
        duration_=duration;started_=GetTickCount64();
        if(record)log_.open(".scratch/osd-independente/integration-live.jsonl",std::ios::app);
        background_=CreateSolidBrush(RGB(20,24,32));
        WNDCLASSW wc{};wc.hInstance=instance_;wc.lpfnWndProc=proc;wc.lpszClassName=prototypeClass;
        wc.hbrBackground=background_;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        if(!RegisterClassW(&wc))return false;
        window_=CreateWindowExW(0,prototypeClass,L"FanController · Validação OSD independente",WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,CW_USEDEFAULT,900,420,nullptr,nullptr,instance_,this);
        if(!window_)return false;
        ShowWindow(window_,show==SW_HIDE?SW_SHOWNORMAL:show);UpdateWindow(window_);
        thermal_=std::thread([this]{readThermal();});
        SetTimer(window_,updateTimer,250,nullptr);update();return true;
    }
    HWND window()const{return window_;}
private:
    HINSTANCE instance_;HWND window_=nullptr,summary_=nullptr,details_=nullptr,toggle_=nullptr;
    HBRUSH background_=nullptr;HFONT font_=nullptr;
    fanctl::MonitoringService monitor_;
    OverlayWindow overlay_;
    PresentMonFrames frames_;
    DWORD tracked_=0;
    bool enabled_=true;
    int duration_=0;
    ULONGLONG started_=0,lastLog_=0;
    std::atomic<bool> stopping_{false};std::thread thermal_;
    std::mutex errorMutex_;std::string thermalError_="Aguardando consulta Dell";
    std::ofstream log_;

    void readThermal() {
        try {
            fanctl::ComRuntime com;
            fanctl::DellReader reader;
            while(!stopping_) {
                try {
                    monitor_.submitThermal({reader.read(),{fanctl::GpuSource::Dell,std::nullopt,{}}});
                    std::lock_guard<std::mutex> lock(errorMutex_);thermalError_.clear();
                }catch(const std::exception& e) {std::lock_guard<std::mutex> lock(errorMutex_);thermalError_=e.what();}
                for(int i=0;i<20 && !stopping_;++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }catch(const std::exception& e){std::lock_guard<std::mutex> lock(errorMutex_);thermalError_=e.what();}
    }
    void controls() {
        const auto create=[&](const wchar_t* kind,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id=0) {
            return CreateWindowExW(0,kind,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance_,nullptr);
        };
        font_=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        create(L"STATIC",L"VALIDAÇÃO · sem RTSS · consultas de hardware somente leitura",0,20,20,830,24);
        create(L"STATIC",L"Faixa real abaixo; aparece no jogo apenas quando ele está em primeiro plano.",0,20,53,830,24);
        summary_=create(L"STATIC",L"Aguardando leituras…",SS_LEFT,20,95,830,48);
        details_=create(L"STATIC",L"",SS_LEFT,20,159,830,140);
        toggle_=create(L"BUTTON",L"Ocultar faixa",WS_TABSTOP|BS_PUSHBUTTON,20,315,180,32,toggleId);
        create(L"BUTTON",L"Encerrar validação",WS_TABSTOP|BS_PUSHBUTTON,220,315,190,32,closeId);
        EnumChildWindows(window_,[](HWND child,LPARAM font)->BOOL{SendMessageW(child,WM_SETFONT,static_cast<WPARAM>(font),TRUE);return TRUE;},reinterpret_cast<LPARAM>(font_));
    }
    void update() {
        if(duration_>0 && GetTickCount64()-started_>=static_cast<ULONGLONG>(duration_)*1000) {DestroyWindow(window_);return;}
        HWND game=fanctl::findEuroTruckWindow();DWORD pid=fanctl::euroTruckProcessId(game);
        if(pid!=tracked_) {
            tracked_=pid;frames_.setTarget(pid);
        }
        const auto sample=monitor_.snapshot();const auto frames=frames_.snapshot();
        const bool foreground=game && game==GetForegroundWindow() && !IsIconic(game);
        const auto fps=foreground?frames.fps:std::nullopt;
        const auto frameMs=foreground?frames.frameTimeMs:std::nullopt;
        fanctl::CompactOsdValues values;
        values.cpuTemperature=sample.cpuTemperature;values.cpuUsage=sample.cpuUsage;
        values.gpuTemperature=sample.gpuTemperature;values.gpuUsage=sample.gpuUsage;
        values.cpuRpm=sample.cpuRpm;values.gpuRpm=sample.gpuRpm;values.fps=fps;values.frameTimeMs=frameMs;
        fanctl::CompactOsdSettings settings;settings.enabled=true;
        const auto text=fanctl::formatCompactOsd(values,settings);
        SetWindowTextW(summary_,text.c_str());
        if(enabled_)overlay_.update(game,text);else overlay_.hide();
        std::string thermalError;{std::lock_guard<std::mutex> lock(errorMutex_);thermalError=thermalError_;}
        const auto status=L"Jogo: "+(pid?std::to_wstring(pid):std::wstring(L"não encontrado"))+
            L" · foco: "+(foreground?L"sim":L"não")+L" · faixa: "+(overlay_.visible()?L"visível":L"oculta")+
            L"\nFrames: "+frames.status+L" · perdas ETW: "+std::to_wstring(frames.eventsLost)+
            L"\nDell: "+(thermalError.empty()?std::wstring(L"leitura disponível (CPU provável e RPM)"):wide(thermalError))+
            L"\n"+overlay_.status()+L"\nFPS: média recente; ms: intervalo entre apresentações da aplicação.";
        SetWindowTextW(details_,status.c_str());
        if(log_ && GetTickCount64()-lastLog_>=1000) {
            lastLog_=GetTickCount64();
            const auto n=[](std::optional<double> v){return v?std::to_string(*v):std::string("null");};
            log_<<"{\"tick_ms\":"<<lastLog_<<",\"pid\":"<<pid<<",\"foreground\":"<<(foreground?"true":"false")
                <<",\"visible\":"<<(overlay_.visible()?"true":"false")<<",\"cpu_usage\":"<<n(sample.cpuUsage)
                <<",\"gpu_usage\":"<<n(sample.gpuUsage)<<",\"gpu_temperature\":"<<n(sample.gpuTemperature)
                <<",\"cpu_temperature\":"<<n(sample.cpuTemperature)<<",\"cpu_rpm\":"<<n(sample.cpuRpm)
                <<",\"gpu_rpm\":"<<n(sample.gpuRpm)<<",\"fps\":"<<n(fps)<<",\"frame_ms\":"<<n(frameMs)
                <<",\"events_lost\":"<<frames.eventsLost<<"}"<<std::endl;
        }
    }
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
        auto* self=reinterpret_cast<Prototype*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE) {self=static_cast<Prototype*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window_=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(window,message,wp,lp);
        switch(message) {
        case WM_CREATE:self->controls();return 0;
        case WM_TIMER:if(wp==updateTimer)self->update();return 0;
        case WM_COMMAND:
            if(LOWORD(wp)==closeId)DestroyWindow(window);
            if(LOWORD(wp)==toggleId){self->enabled_=!self->enabled_;SetWindowTextW(self->toggle_,self->enabled_?L"Ocultar faixa":L"Mostrar faixa");self->update();}return 0;
        case WM_CTLCOLORSTATIC:SetBkColor(reinterpret_cast<HDC>(wp),RGB(20,24,32));SetTextColor(reinterpret_cast<HDC>(wp),RGB(235,238,243));return reinterpret_cast<LRESULT>(self->background_);
        case WM_CLOSE:DestroyWindow(window);return 0;
        case WM_DESTROY:KillTimer(window,updateTimer);self->overlay_.hide();PostQuitMessage(0);return 0;
        }
        return DefWindowProcW(window,message,wp,lp);
    }
};
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE singleton=CreateMutexW(nullptr,TRUE,L"Local\\FanController.IndependentOverlay.Validation");
    if(!singleton)return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(singleton);return 0;}
    int result=0;
    try {
        int argc=0;LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&argc);int duration=0;bool record=false;
        for(int i=1;args && i<argc;++i) {if(std::wstring(args[i])==L"--record")record=true;else if(std::wstring(args[i])==L"--seconds" && i+1<argc)duration=_wtoi(args[++i]);}
        if(args)LocalFree(args);
        Prototype prototype(instance);
        if(!prototype.create(show,duration,record))throw std::runtime_error("Não foi possível criar o protótipo.");
        MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(prototype.window(),&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    }catch(const std::exception& error){MessageBoxW(nullptr,wide(error.what()).c_str(),L"Protótipo OSD",MB_OK|MB_ICONERROR);result=1;}
    ReleaseMutex(singleton);CloseHandle(singleton);return result;
}


