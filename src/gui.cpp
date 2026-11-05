#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include "web_shell.h"
#include "history_presentation.h"
#include "history_service.h"
#include "windows_backend.h"
#include "nvml_temperature.h"
#include "automatic_profiles.h"
#include "startup_settings.h"
#include "persistent_files.h"
#include "journal_recovery.h"
#include "monitoring.h"
#include "compact_osd.h"
#include "overlay_window.h"
#include "presentmon_frames.h"
#include <fstream>
#include <filesystem>

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace {
constexpr wchar_t windowClass[]=L"FanController.Window";
constexpr wchar_t previewWindowClass[]=L"FanController.Preview.Window";
constexpr UINT trayMessage=WM_APP+1, stateMessage=WM_APP+2, exitMessage=WM_APP+3;
constexpr int applyId=100, maximumId=101, restoreId=102, sourceId=103, exitId=104;
constexpr int openMenu=4001, restoreMenu=4002, exitMenu=4003;
constexpr int modeId=105,saveCurvesId=106,editProfileId=107;
constexpr int historyPeriodId=108;
constexpr int osdEnableId=109,osdMetricId=120,osdMenu=4004;
constexpr UINT historyMessage=WM_APP+4;

std::wstring curvesPath() {
    wchar_t path[32768]{};
    const auto length=GetModuleFileNameW(nullptr,path,32768);
    if (!length || length>=32768) throw std::runtime_error("Caminho de curvas indisponivel.");
    return (std::filesystem::path(path).parent_path()/L"fanctl-curves.txt").wstring();
}
fanctl::CurveSettings loadCurves() {
    const auto path=curvesPath();
    if (GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES) {
        if (GetLastError()==ERROR_FILE_NOT_FOUND) return fanctl::defaultCurves();
        throw std::runtime_error("Nao foi possivel acessar as curvas salvas.");
    }
    std::ifstream file{std::filesystem::path(path)};
    if (!file) throw std::runtime_error("Nao foi possivel ler as curvas salvas.");
    std::string text((std::istreambuf_iterator<char>(file)),{});
    return fanctl::decodeCurves(text);
}
void saveCurves(const fanctl::CurveSettings& settings) {
    const auto text=fanctl::encodeCurves(settings);
    const auto destination=curvesPath(),temporary=destination+L".tmp";
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) throw std::runtime_error("Nao foi possivel salvar as curvas.");
    DWORD written=0;
    const bool success=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr) &&
        written==text.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!success || !MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());throw std::runtime_error("Nao foi possivel concluir o salvamento das curvas.");
    }
}

std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const int count=MultiByteToWideChar(CP_UTF8,0,text.c_str(),-1,nullptr,0);
    if (!count) return L"Erro de leitura.";
    std::wstring result(static_cast<std::size_t>(count),L'\0');
    MultiByteToWideChar(CP_UTF8,0,text.c_str(),-1,result.data(),count);
    result.resize(static_cast<std::size_t>(count-1));
    return result;
}

HICON fanIcon() {
    if(auto icon=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101),IMAGE_ICON,32,32,0)))return icon;
    BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=32;info.bmiHeader.biHeight=-32;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;
    HBITMAP color=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if (!color) return nullptr;
    auto* pixels=static_cast<DWORD*>(bits);
    for (int y=0;y<32;++y) for (int x=0;x<32;++x) {
        DWORD pixel=0;
        for(int curve=0;curve<2;++curve)for(int step=0;step<=100;++step){
            const double t=step/100.,u=1-t;
            const double px=curve?u*u*u*5+3*u*u*t*14+3*u*t*t*24+t*t*t*24:u*u*u*4+3*u*u*t*15+3*u*t*t*29+t*t*t*29;
            const double py=curve?u*u*u*22+3*u*u*t*17+3*u*t*t*17+t*t*t*27:u*u*u*12+3*u*u*t*5+3*u*t*t*6+t*t*t*17;
            if((x-px)*(x-px)+(y-py)*(y-py)<2.2)pixel=0xff65d7c7;
        }
        pixels[y*32+x]=pixel;
    }
    BYTE maskBits[128]{};
    HBITMAP mask=CreateBitmap(32,32,1,1,maskBits);
    ICONINFO iconInfo{TRUE,0,0,mask,color};
    HICON icon=CreateIconIndirect(&iconInfo);
    DeleteObject(mask);DeleteObject(color);
    return icon;
}

enum class Action { Apply, Restore, Source, Exit, Curves };
struct Request {
    Action action; int first=30;int second=30;bool nvml=false;
    int mode=0;
    int startupAttempt=-1;
    fanctl::CurveSettings curves=fanctl::defaultCurves();
};
struct View {
    std::optional<fanctl::ControlFrame> frame;
    std::wstring error;
    bool active=false,pending=false,busy=true,ready=false,nvml=false,attention=false;
    int mode=0;
    std::string configuration;
};

class Application {
public:
    Application(HINSTANCE instance,bool startup,bool preview=false):instance_(instance),startupLaunch_(startup),preview_(preview){if(preview_)view_.busy=false;}
    ~Application(){if(worker_.joinable())worker_.join();if(monitoring_)monitoring_->stop();if(overlay_)overlay_->hide();if(frames_)frames_->stop();if(trayAdded_)Shell_NotifyIconW(NIM_DELETE,&tray_);if(icon_)DestroyIcon(icon_);if(background_)DeleteObject(background_);}
    bool create(int show) {
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES|ICC_STANDARD_CLASSES|ICC_TAB_CLASSES};
        if (!InitCommonControlsEx(&controls)) return false;
        dpi_=GetDpiForSystem();
        WNDCLASSW type{};
        type.lpfnWndProc=windowProc;type.hInstance=instance_;type.lpszClassName=preview_ ? previewWindowClass : windowClass;
        type.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        background_=CreateSolidBrush(RGB(19,34,40));
        type.hbrBackground=background_;
        icon_=fanIcon();type.hIcon=icon_;
        if (!RegisterClassW(&type)) return false;
        const DWORD style=WS_OVERLAPPEDWINDOW;
        RECT workArea{};
        if (SystemParametersInfoW(SPI_GETWORKAREA,0,&workArea,0))
            clientHeight_=std::clamp(MulDiv(workArea.bottom-workArea.top,96,static_cast<int>(dpi_))-60,640,740);
        RECT bounds{0,0,scale(1100),scale(clientHeight_)};
        AdjustWindowRectExForDpi(&bounds,style,FALSE,0,dpi_);
        window_=CreateWindowExW(WS_EX_CONTROLPARENT,type.lpszClassName,preview_ ? L"aeri — Prévia" : L"aeri",style,
            CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,
            nullptr,nullptr,instance_,this);
        if (!window_) return false;
        ShowWindow(window_,preview_ ? SW_SHOWNORMAL : startupLaunch_ && trayAdded_ && settingsError_.empty() ? SW_HIDE : show);UpdateWindow(window_);
        if (startupLaunch_ && settingsError_.empty()) {
            Request request{Action::Apply,startup_.first,startup_.second};
            request.mode=startup_.mode;request.curves=curves_;request.startupAttempt=0;requests_.push_back(request);
        }
        if (!preview_) {
            try {
                overlay_=std::make_unique<fanctl::OverlayWindow>();
                frames_=std::make_unique<fanctl::PresentMonFrames>();
            } catch (const std::exception& error) {
                osdError_=L"Faixa indisponível: "+wide(error.what());
                overlay_.reset();frames_.reset();
            }
            monitoring_=std::make_unique<fanctl::MonitoringService>();
            worker_=std::thread([this] {work();});
        }
        SetTimer(window_,1,1000,nullptr);
        refreshMonitoring();
        return true;
    }
    HWND window()const{return window_;}
private:
    HINSTANCE instance_;HWND window_=nullptr;UINT dpi_=96,taskbarCreated_=0;
    int clientHeight_=740,clientWidth_=1100;
    HBRUSH background_=nullptr;HICON icon_=nullptr;
    NOTIFYICONDATAW tray_{};bool trayAdded_=false,startupLaunch_=false,preview_=false;
    std::thread worker_;std::mutex mutex_;std::condition_variable wake_;std::deque<Request> requests_;
    View view_;std::atomic<bool> cancel_{false};
    fanctl::CurveSettings curves_=fanctl::defaultCurves();fanctl::StartupSettings startup_;
    std::wstring settingsError_,osdError_,osdStatus_;
    std::unique_ptr<fanctl::HistoryService> history_;
    std::unique_ptr<fanctl::MonitoringService> monitoring_;
    fanctl::CompactOsdSettings osdSettings_;fanctl::CompactOsdValues liveValues_;
    std::unique_ptr<fanctl::OverlayWindow> overlay_;std::unique_ptr<fanctl::PresentMonFrames> frames_;
    fanctl::WebShell web_;bool webReady_=false,webCreated_=false,webHistoryVisible_=false,glass_=true,glassAvailable_=false;
    int scale(int value)const{return MulDiv(value,static_cast<int>(dpi_),96);}
    #include "aeri_ui.inc"
    void createControls(){
        try{curves_=loadCurves();}catch(const std::exception& error){settingsError_=wide(error.what());}
        try{if(const auto saved=fanctl::readPersistent(L"fanctl-startup.txt"))startup_=fanctl::decodeStartup(*saved);}catch(const std::exception& error){settingsError_+=L" "+wide(error.what());}
        try{osdSettings_=fanctl::loadCompactOsdSettings();}catch(const std::exception& error){osdError_=wide(error.what());}
        if(!preview_)history_=std::make_unique<fanctl::HistoryService>([this]{PostMessageW(window_,historyMessage,0,0);});
        taskbarCreated_=RegisterWindowMessageW(L"TaskbarCreated");addTray();createAeri();
    }
    View current(){std::lock_guard<std::mutex> lock(mutex_);return view_;}
    void render(){const auto view=current();if(view.attention){show();std::lock_guard<std::mutex> lock(mutex_);view_.attention=false;}if(trayAdded_){const auto tip=view.pending?L"aeri — restauração pendente":view.active?L"aeri — controle ativo":L"aeri — monitorando";wcscpy_s(tray_.szTip,tip);tray_.uFlags=NIF_TIP;Shell_NotifyIconW(NIM_MODIFY,&tray_);}if(IsWindowVisible(window_))publishWebState();}
    void addTray() {
        tray_={};tray_.cbSize=sizeof(tray_);tray_.hWnd=window_;tray_.uID=1;
        tray_.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray_.uCallbackMessage=trayMessage;tray_.hIcon=icon_;
        wcscpy_s(tray_.szTip,L"aeri — monitorando");
        trayAdded_=Shell_NotifyIconW(NIM_ADD,&tray_)!=FALSE;
        if (trayAdded_) {tray_.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&tray_);}
    }
    void show() {ShowWindow(window_,SW_RESTORE);SetForegroundWindow(window_);if(!webCreated_){webCreated_=true;web_.create(window_,preview_);}else{web_.visible(true);web_.post(L"{\"type\":\"visibility\",\"visible\":true}");}refreshMonitoring();}
    void hide() {if(trayAdded_){webHistoryVisible_=false;if(history_)history_->period(900);web_.visible(false);ShowWindow(window_,SW_HIDE);}else MessageBoxW(window_,L"A bandeja está indisponível. Use Encerrar para restaurar e sair.",L"aeri",MB_OK|MB_ICONINFORMATION);}
    void refreshMonitoring(bool record=true) {
        fanctl::CompactOsdValues values;
        if (preview_) {
            const double t=GetTickCount64()/1000.0;
            values.cpuTemperature=62+8*std::sin(t/9);values.cpuUsage=35+15*std::sin(t/6);
            values.gpuTemperature=57+6*std::sin(t/12);values.gpuUsage=72+17*std::sin(t/8);
            values.cpuRpm=3200;values.gpuRpm=3000;values.fps=75;values.frameTimeMs=13.3;
        } else {
            if (monitoring_) {
                const auto snapshot=monitoring_->snapshot();
                values.cpuTemperature=snapshot.cpuTemperature;values.gpuTemperature=snapshot.gpuTemperature;
                values.cpuUsage=snapshot.cpuUsage;values.gpuUsage=snapshot.gpuUsage;

                values.cpuRpm=snapshot.cpuRpm;values.gpuRpm=snapshot.gpuRpm;
            }
            try {
                HWND game=fanctl::findEuroTruckWindow();
                const DWORD pid=game ? fanctl::euroTruckProcessId(game) : 0;
                const bool needFrames=osdSettings_.enabled && (osdSettings_.metrics&192u) && game && !IsIconic(game);
                if (frames_) {
                    frames_->setTarget(needFrames ? pid : 0);
                    const auto frame=frames_->snapshot();
                    if (frame.processId==pid && pid) {values.fps=frame.fps;values.frameTimeMs=frame.frameTimeMs;}
                    if (osdSettings_.enabled && pid && !frame.fps) osdStatus_=frame.status;
                }
                if (overlay_ && osdSettings_.enabled) {
                    overlay_->update(game,fanctl::formatCompactOsd(values,osdSettings_));
                    if (!pid) osdStatus_=L"Aguardando Euro Truck Simulator 2";
                    else if (values.fps || !(osdSettings_.metrics&192u)) osdStatus_=overlay_->status();
                } else {if(overlay_)overlay_->hide();osdStatus_=L"Faixa desligada";}
            } catch (const std::exception&) {
                if(overlay_)overlay_->hide();
                osdStatus_=L"OSD indisponível · controle das fans preservado";
            }
        }
        if (preview_) osdStatus_=L"Prévia visual · sem coleta ou sobreposição sobre o jogo";
        if (!osdError_.empty()) osdStatus_=osdError_;
        liveValues_=values;if(record&&IsWindowVisible(window_)&&!IsIconic(window_))publishWebState();
    }
    void send(Request request) {
        if(preview_){if(request.action==Action::Exit)DestroyWindow(window_);else if(request.action==Action::Restore){std::lock_guard<std::mutex> lock(mutex_);view_.active=false;view_.mode=0;}publishWebState();return;}
        {std::lock_guard<std::mutex> lock(mutex_);
            requests_.push_back(request);view_.busy=true;
            if (request.action==Action::Restore || request.action==Action::Exit) cancel_=true;
        }
        wake_.notify_one();render();
    }
    void renderHistory() {
        fanctl::HistoryView history;
        if(!history_){history.from=fanctl::historyNow()-3600;history.until=fanctl::historyNow();history.loading=false;auto samples=std::make_shared<std::vector<fanctl::HistorySample>>();for(int i=0;i<=900;++i){if(i>660&&i<710)continue;fanctl::HistorySample row;row.time=row.end=history.from+i*4;row.gpuSource=fanctl::GpuSource::Nvml;row.interval=4;row.segment=i<660?"a":"b";row.configuration=i<450?"Normal · revisão 1":"Normal · revisão 2";row.fans[0].temperature=static_cast<int>(60+10*std::sin(i/65.));row.fans[1].temperature=static_cast<int>(54+8*std::sin(i/82.));row.fans[0].rpm=static_cast<int>(2600+700*std::sin(i/65.));row.fans[1].rpm=static_cast<int>(2400+600*std::sin(i/82.));samples->push_back(row);}history.samples=samples;history.events={{history.from+1800,"Curvas aplicadas","Normal · revisão 2\nCPU: 50 °C → boost 20; 90 °C → boost 100"}};}
        else history=history_->view();
        if(webHistoryVisible_)publishWebHistory(history);
    }
    void menu() {
        HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,openMenu,L"Abrir aeri");
        AppendMenuW(menu,MF_STRING|(osdSettings_.enabled ? MF_CHECKED : 0),osdMenu,L"Mostrar OSD no jogo");
        AppendMenuW(menu,MF_STRING,restoreMenu,L"Restaurar ajustes anteriores");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,exitMenu,L"Sair e restaurar");
        const auto view=current();EnableMenuItem(menu,restoreMenu,MF_BYCOMMAND|(view.active && !view.busy ? MF_ENABLED : MF_GRAYED));
        POINT point{};GetCursorPos(&point);SetForegroundWindow(window_);
        const auto selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,point.x,point.y,0,window_,nullptr);
        DestroyMenu(menu);PostMessageW(window_,WM_NULL,0,0);
        if (selected==openMenu) show();
        else if(selected==osdMenu){auto settings=osdSettings_;settings.enabled=!settings.enabled;try{if(!preview_)fanctl::saveCompactOsdSettings(settings);osdSettings_=settings;refreshMonitoring();}catch(const std::exception& error){MessageBoxW(window_,wide(error.what()).c_str(),L"aeri",MB_OK|MB_ICONERROR);}}
        else if (selected==restoreMenu) {show();send({Action::Restore});}
        else if (selected==exitMenu) send({Action::Exit});
    }

    void work() {
        try {
            fanctl::ComRuntime runtime;
            std::unique_ptr<fanctl::DellReader> reader;
            for (int attempt=0;!reader;++attempt) {
                try {reader=std::make_unique<fanctl::DellReader>();}
                catch (const std::exception&) {
                    if (!startupLaunch_ || attempt>=11) throw;
                    std::unique_lock<std::mutex> lock(mutex_);
                    wake_.wait_for(lock,std::chrono::seconds(5),[this] {return cancel_.load();});
                    if (cancel_) {PostMessageW(window_,exitMessage,0,0);return;}
                }
            }
            auto& backend=*reader;
            if (startupLaunch_) {
                fanctl::BoostLock recoveryLease;
                if (const auto journal=fanctl::readPersistent(L"fanctl-session.txt")) {
                    fanctl::recoverJournal(backend,fanctl::decodeJournal(*journal));
                    if (!DeleteFileW(fanctl::dataPath(L"fanctl-session.txt").c_str()))
                        throw std::runtime_error("Estado recuperado, mas registro nao removido.");
                }
            }
            bool nvml=startup_.nvml,manualNvml=startup_.nvml,pending=false,transientError=false;
            int mode=0;
            auto settings=curves_;
            std::optional<fanctl::AutomaticPolicy> automatic;
            std::wstring error;
            std::unique_ptr<fanctl::GpuMonitor> monitor;
            std::unique_ptr<fanctl::ControlSession> session;
            auto makeMonitor=[&] {monitor=std::make_unique<fanctl::GpuMonitor>(nvml ? fanctl::GpuSource::Nvml : fanctl::GpuSource::Dell,
                [] {return std::make_unique<fanctl::NvmlTemperature>();});};
            makeMonitor();
            auto restore=[&]() {
                if (!session) return true;
                const auto result=session->restore();
                if (!result.verified) {
                    pending=true;error=L"Restauração não confirmada. Clique em Tentar restaurar. O registro foi preservado.";
                    for (const auto& message : result.messages) error+=L" "+wide(message);
                    return false;
                }
                session.reset();automatic.reset();mode=0;pending=false;
                if (nvml!=manualNvml) {nvml=manualNvml;makeMonitor();}
                return true;
            };
            bool first=true;
            std::string configuration="Controle original · monitoramento";
            int revision=0;
            const auto describe=[&](const Request& request) {
                std::ostringstream out;
                out<<(request.mode==1?"Normal":request.mode==2?"Performance":"Manual")<<" · revisão "<<++revision<<" · aplicado "<<fanctl::historyNow()<<'\n';
                if(request.mode){for(int fan=0;fan<2;++fan){out<<(fan?"GPU: ":"CPU: ");for(const auto& point:request.curves[request.mode-1][fan])out<<point.temperature<<" °C → boost "<<point.boost<<"; ";out<<'\n';}}
                else out<<"Boost solicitado: CPU "<<request.first<<" · GPU "<<request.second;
                return out.str();
            };
            std::optional<Request> retry;
            auto retryAt=std::chrono::steady_clock::now();
            while (true) {
                std::optional<Request> request;
                if (!first) {
                    std::unique_lock<std::mutex> lock(mutex_);
                    wake_.wait_for(lock,std::chrono::seconds(2),[this] {return !requests_.empty();});
                    if (!requests_.empty()) {request=requests_.front();requests_.pop_front();retry.reset();}
                    else if (retry && std::chrono::steady_clock::now()>=retryAt) {request=*retry;retry.reset();}
                }
                first=false;
                bool attention=false;
                bool activeCurvesChanged=false;
                std::optional<fanctl::ControlFrame> frame;
                std::optional<fanctl::ControlFrame> partialHistoryFrame;
                const auto readAttempt=backend.readAttempt();
                try {
                    if (request) {
                        if (request->action==Action::Exit || request->action==Action::Restore) {
                            if (restore()) {
                                configuration="Controle original · restauração confirmada";
                                history_->event("Controle original restaurado",configuration);
                                error.clear();
                                if (request->action==Action::Exit) {PostMessageW(window_,exitMessage,0,0);return;}
                            } else attention=true;
                            cancel_=false;
                        } else if (request->action==Action::Source) {
                            if (session) throw std::runtime_error("Restaure os ajustes antes de mudar a fonte GPU.");
                            nvml=manualNvml=request->nvml;makeMonitor();error.clear();transientError=false;
                        } else if (request->action==Action::Curves) {
                            activeCurvesChanged=automatic && !fanctl::sameCurves(settings[mode-1],request->curves[mode-1]);
                            settings=request->curves;
                            if (activeCurvesChanged) automatic->replaceCurves(settings[mode-1]);
                        } else if (request->action==Action::Apply) {
                            if (!restore()) attention=true;
                            else {
                                error.clear();transientError=false;
                                {std::lock_guard<std::mutex> lock(mutex_);
                                    bool stopQueued=false;
                                    for (const auto& queued : requests_)
                                        if (queued.action==Action::Exit || queued.action==Action::Restore) stopQueued=true;
                                    cancel_=stopQueued;
                                }
                                mode=request->mode;settings=request->curves;
                                std::vector<fanctl::Adjustment> targets{{50,static_cast<std::uint32_t>(request->first)},{51,static_cast<std::uint32_t>(request->second)}};
                                if (mode) {
                                    fanctl::validateCurves(settings);
                                    nvml=true;makeMonitor();
                                    automatic.emplace(settings[mode-1]);
                                    auto snapshot=backend.read();auto gpu=fanctl::selectedGpu(snapshot,*monitor);
                                    // Validate required temperatures before any firmware write.
                                    const fanctl::ControlFrame initial{std::move(snapshot),std::move(gpu)};
                                    fanctl::automaticTemperatures(initial);
                                    auto starting=initial;
                                    for (auto& fan : starting.snapshot.fans) fan.rpm=1; // Zero-RPM timer starts after activation.
                                    targets=automatic->next(starting,fanctl::AutomaticPolicy::Clock::now());
                                }
                                session=std::make_unique<fanctl::ControlSession>(backend,*monitor,targets,
                                    std::make_unique<fanctl::WindowsSessionResources>());
                                session->apply([this] {return cancel_.load();});
                                if (!cancel_) {
                                    session->sample();
                                    fanctl::writePersistent(L"fanctl-startup.txt",fanctl::encodeStartup(
                                        {mode,request->first,request->second,manualNvml}));
                                }
                            }
                        }
                    }
                    if (session && !pending) {
                        frame=session->sample();
                        if (automatic) {
                            const auto targets=automatic->next(*frame,fanctl::AutomaticPolicy::Clock::now());
                            session->update(targets,[this] {return cancel_.load();});
                            frame=session->sample();
                        }
                    }
                    else {
                        auto snapshot=backend.read();auto gpu=fanctl::selectedGpu(snapshot,*monitor);
                        if (gpu.source==fanctl::GpuSource::Nvml && !gpu.temperature) {
                            error=L"GPU NVML indisponível: "+wide(gpu.error);transientError=true;
                        } else if (transientError && !pending) {error.clear();transientError=false;}
                        frame=fanctl::ControlFrame{std::move(snapshot),std::move(gpu)};
                    }
                    if(request && !pending && frame && session &&
                        (request->action==Action::Apply || activeCurvesChanged)) {
                        Request confirmed=*request;confirmed.mode=mode;confirmed.curves=settings;
                        configuration=describe(confirmed);
                        history_->event(request->action==Action::Curves?"Curvas aplicadas ao perfil ativo":"Perfil ativado",configuration);
                    }
                } catch (const std::exception& exception) {
                    // Keep telemetry obtained before a failed query; never reuse an older snapshot.
                    if (backend.readAttempt()!=readAttempt) {
                        auto partial=backend.partialSnapshot();
                        partialHistoryFrame=fanctl::ControlFrame{partial,fanctl::selectedGpu(partial,*monitor)};
                    }
                    error=wide(exception.what());attention=true;transientError=!session;
                    if (session && !restore()) attention=true;
                    configuration=session?"Restauração pendente · configuração não confirmada":"Controle original · recuperação após falha";
                    history_->event("Falha de controle ou leitura",configuration);
                    if (!session) {automatic.reset();mode=0;if (nvml!=manualNvml) {nvml=manualNvml;makeMonitor();}}
                    if (request && request->action==Action::Apply && request->startupAttempt>=0 &&
                        request->startupAttempt<11 && !session && !pending && !cancel_) {
                        retry=*request;++retry->startupAttempt;retryAt=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                        error=L"Aguardando a interface térmica para retomar o perfil…";attention=false;transientError=false;
                    }
                }
                history_->offer(frame ? frame : partialHistoryFrame,nvml ? fanctl::GpuSource::Nvml : fanctl::GpuSource::Dell,configuration);
                if (monitoring_ && frame) monitoring_->submitThermal(*frame);
                {std::lock_guard<std::mutex> lock(mutex_);
                    view_.frame=std::move(frame);view_.error=error;view_.active=session!=nullptr;view_.pending=pending;
                    view_.nvml=nvml;view_.ready=true;view_.busy=!requests_.empty();view_.attention=attention;
                    view_.mode=mode;
                    view_.configuration=configuration;
                }
                PostMessageW(window_,stateMessage,0,0);
            }
        } catch (const std::exception& exception) {
            {std::lock_guard<std::mutex> lock(mutex_);view_.error=wide(exception.what());view_.busy=false;view_.attention=true;}
            PostMessageW(window_,stateMessage,0,0);
            // No session was started if initialization failed. Keep the window usable for Exit.
            while (true) {
                std::unique_lock<std::mutex> lock(mutex_);wake_.wait(lock,[this] {return !requests_.empty();});
                const auto request=requests_.front();requests_.pop_front();
                if (request.action==Action::Exit) {PostMessageW(window_,exitMessage,0,0);return;}
            }
        }
    }

    static LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM wp,LPARAM lp){
        auto* app=reinterpret_cast<Application*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){app=static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);app->window_=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
        if(!app)return DefWindowProcW(window,message,wp,lp);
        if(app->taskbarCreated_&&message==app->taskbarCreated_){app->addTray();return 0;}
        switch(message){
        case WM_CREATE:app->createControls();return 0;
        case WM_CLOSE:if(app->preview_)DestroyWindow(window);else app->hide();return 0;
        case WM_TIMER:if(wp==1)app->refreshMonitoring();return 0;
        case WM_ACTIVATE:case WM_DWMCOMPOSITIONCHANGED:app->backdrop();break;
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT ps{};auto dc=BeginPaint(window,&ps);RECT r{};GetClientRect(window,&r);auto brush=CreateSolidBrush(app->glassAvailable_?RGB(0,0,0):RGB(19,34,40));FillRect(dc,&r,brush);DeleteObject(brush);EndPaint(window,&ps);return 0;}
        case WM_GETMINMAXINFO:{auto* limits=reinterpret_cast<MINMAXINFO*>(lp);RECT r{0,0,app->scale(900),app->scale(640)};AdjustWindowRectExForDpi(&r,WS_OVERLAPPEDWINDOW,FALSE,0,app->dpi_);limits->ptMinTrackSize={r.right-r.left,r.bottom-r.top};return 0;}
        case WM_SIZE:if(wp==SIZE_MINIMIZED&&app->trayAdded_)app->hide();else{app->clientWidth_=MulDiv(LOWORD(lp),96,app->dpi_);app->clientHeight_=MulDiv(HIWORD(lp),96,app->dpi_);app->web_.resize();}return 0;
        case WM_DPICHANGED:{app->dpi_=HIWORD(wp);const auto* r=reinterpret_cast<RECT*>(lp);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);app->web_.resize();return 0;}
        case WM_QUERYENDSESSION:{app->send({Action::Restore});const auto until=GetTickCount64()+4000;while(app->current().busy&&GetTickCount64()<until)Sleep(20);return TRUE;}
        case WM_ENDSESSION:if(wp)app->send({Action::Exit});return 0;
        case trayMessage:if(LOWORD(lp)==WM_CONTEXTMENU)app->menu();else if(LOWORD(lp)==NIN_SELECT||LOWORD(lp)==NIN_KEYSELECT)app->show();return 0;
        case stateMessage:app->render();return 0;
        case historyMessage:app->renderHistory();return 0;
        case exitMessage:DestroyWindow(window);return 0;
        case WM_DESTROY:KillTimer(window,1);if(app->overlay_)app->overlay_->hide();if(app->frames_)app->frames_->setTarget(0);PostQuitMessage(0);return 0;
        }return DefWindowProcW(window,message,wp,lp);
    }
};
} // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR arguments,int show) {
    const auto apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(apartment))return 1;
    const std::wstring argument=arguments ? arguments : L"";
    const bool startup=argument==L"--startup";
#ifdef FANCTL_PREVIEW_ONLY
    bool preview=true;
#else
    const bool preview=argument==L"--preview";
#endif
    if (!argument.empty() && !startup && !preview) return 2;
    if (!preview && !fanctl::isAdministrator()) {
        MessageBoxW(nullptr,L"Abra o FanController como administrador para acessar a interface Dell.",L"aeri",MB_OK|MB_ICONINFORMATION);
        return 1;
    }
    HANDLE singleton=CreateMutexW(nullptr,TRUE,preview ? L"Local\\FanControllerGuiPreview" : L"Local\\FanControllerGui");
    if (!singleton) return 1;
    if (GetLastError()==ERROR_ALREADY_EXISTS) {
        if (!startup) if (HWND existing=FindWindowW(preview ? previewWindowClass : windowClass,nullptr)) {ShowWindow(existing,SW_RESTORE);SetForegroundWindow(existing);}
        CloseHandle(singleton);return 0;
    }
    int result=0;
    {
        Application application(instance,startup,preview);
        if (!application.create(show)) {
            MessageBoxW(nullptr,L"Não foi possível criar a janela.",L"aeri",MB_OK|MB_ICONERROR);result=1;
        } else {
            MSG message{};
            while (GetMessageW(&message,nullptr,0,0)>0) {
                if (!IsDialogMessageW(application.window(),&message)) {TranslateMessage(&message);DispatchMessageW(&message);}
            }
        }
    }
    ReleaseMutex(singleton);CloseHandle(singleton);
    CoUninitialize();
    return result;
}
