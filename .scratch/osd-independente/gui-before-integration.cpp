#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include "windows_backend.h"
#include "nvml_temperature.h"
#include "automatic_profiles.h"
#include "startup_settings.h"
#include "persistent_files.h"
#include "journal_recovery.h"
#include "history_chart.h"
#include "monitoring.h"
#include "osd.h"
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
constexpr int osdEnableId=109,osdCornerId=110,openRtssId=111,osdMetricId=120,osdMenu=4004;
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
    BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=32;info.bmiHeader.biHeight=-32;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;
    HBITMAP color=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if (!color) return nullptr;
    auto* pixels=static_cast<DWORD*>(bits);
    for (int y=0;y<32;++y) for (int x=0;x<32;++x) {
        const double dx=x-15.5,dy=y-15.5,r=std::sqrt(dx*dx+dy*dy);
        DWORD pixel=r<=15 ? 0xff0e7985 : 0;
        const double angle=std::atan2(dy,dx);
        const bool blade=r>4 && r<12 && std::sin(3*angle+r*0.13)>0.05;
        if (r<3 || blade) pixel=0xfff6fafb;
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
};

class Application {
public:
    explicit Application(HINSTANCE instance,bool startup,bool preview=false) : instance_(instance),startupLaunch_(startup),preview_(preview) {
        if (preview_) view_.busy=false;
    }
    ~Application() {
        if (worker_.joinable()) worker_.join();
        if (monitoring_) monitoring_->stop();
        if (rtss_) rtss_->release();
        if (trayAdded_) Shell_NotifyIconW(NIM_DELETE,&tray_);
        if (icon_) DestroyIcon(icon_);
        for (auto font : {normalFont_,titleFont_,rpmFont_}) if (font) DeleteObject(font);
        if (background_) DeleteObject(background_);
    }
    bool create(int show) {
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES|ICC_STANDARD_CLASSES|ICC_TAB_CLASSES};
        if (!InitCommonControlsEx(&controls)) return false;
        dpi_=GetDpiForSystem();
        WNDCLASSW type{};
        type.lpfnWndProc=windowProc;type.hInstance=instance_;type.lpszClassName=preview_ ? previewWindowClass : windowClass;
        type.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        background_=CreateSolidBrush(RGB(19,24,32));
        type.hbrBackground=background_;
        icon_=fanIcon();type.hIcon=icon_;
        if (!RegisterClassW(&type)) return false;
        const DWORD style=WS_OVERLAPPEDWINDOW;
        RECT workArea{};
        if (SystemParametersInfoW(SPI_GETWORKAREA,0,&workArea,0))
            clientHeight_=std::clamp(MulDiv(workArea.bottom-workArea.top,96,static_cast<int>(dpi_))-60,640,740);
        RECT bounds{0,0,scale(640),scale(clientHeight_)};
        AdjustWindowRectExForDpi(&bounds,style,FALSE,0,dpi_);
        window_=CreateWindowExW(WS_EX_CONTROLPARENT,type.lpszClassName,preview_ ? L"FanController — Prévia" : L"FanController",style,
            CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,
            nullptr,nullptr,instance_,this);
        if (!window_) return false;
        ShowWindow(window_,preview_ ? SW_SHOWNORMAL : startupLaunch_ && trayAdded_ && settingsError_.empty() ? SW_HIDE : show);UpdateWindow(window_);
        if (startupLaunch_ && settingsError_.empty()) {
            Request request{Action::Apply,startup_.first,startup_.second};
            request.mode=startup_.mode;request.curves=curves_;request.startupAttempt=0;requests_.push_back(request);
        }
        if (!preview_) {
            rtss_=std::make_unique<fanctl::RtssOsd>();
            monitoring_=std::make_unique<fanctl::MonitoringService>();
            worker_=std::thread([this] {work();});
        }
        SetTimer(window_,1,1000,nullptr);
        refreshMonitoring();
        return true;
    }
    HWND window() const {return window_;}
private:
    HINSTANCE instance_;
    HWND window_=nullptr;
    UINT dpi_=96,taskbarCreated_=0;
    int clientHeight_=740,clientWidth_=640;
    HBRUSH background_=nullptr;
    HICON icon_=nullptr;
    HFONT normalFont_=nullptr,titleFont_=nullptr,rpmFont_=nullptr;
    HWND title_=nullptr,subtitle_=nullptr,status_=nullptr,group_[2]{},rpm_[2]{},actual_[2]{},temperature_[2]{},target_[2]{},slider_[2]{};
    HWND source_=nullptr,hint_=nullptr,apply_=nullptr,maximum_=nullptr,restore_=nullptr,footer_=nullptr,updated_=nullptr,exit_=nullptr;
    HWND tabs_=nullptr,mode_=nullptr,editProfile_=nullptr,saveCurves_=nullptr,curveHint_=nullptr;
    HWND curveHeaders_[4]{},curveEdits_[2][5][2]{};
    std::vector<HWND> mainControls_,curveControls_;
    std::vector<HWND> historyControls_;
    HWND historyPeriod_=nullptr,historyNotice_=nullptr,historyLabel_=nullptr,historyWindows_[2]{};
    fanctl::HistoryChart historyCharts_[2];
    std::unique_ptr<fanctl::HistoryService> history_;
    fanctl::CurveSettings curves_=fanctl::defaultCurves();
    int editingProfile_=0;
    std::wstring settingsError_;
    bool startupLaunch_=false;
    fanctl::StartupSettings startup_;
    HWND modeLabel_=nullptr;
    NOTIFYICONDATAW tray_{};
    bool trayAdded_=false;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Request> requests_;
    View view_;
    std::atomic<bool> cancel_{false};
    bool preview_=false;
    std::unique_ptr<fanctl::MonitoringService> monitoring_;
    fanctl::OsdSettings osdSettings_;
    std::unique_ptr<fanctl::RtssOsd> rtss_;
    fanctl::OsdValues liveValues_;
    std::vector<HWND> dashboardControls_,osdControls_;
    HWND dashboard_=nullptr,osdEnabled_=nullptr,osdCorner_=nullptr,osdNotice_=nullptr,osdOpen_=nullptr;
    HWND osdChecks_[8]{},osdTitle_=nullptr,osdCornerLabel_=nullptr,osdHelp_=nullptr;
    std::deque<fanctl::OsdValues> recent_;
    std::wstring osdError_;
    bool gpuUsageFromWindows_=false;

    int scale(int value) const {return MulDiv(value,static_cast<int>(dpi_),96);}
    HWND control(const wchar_t* type,const wchar_t* text,DWORD style,int id=0) {
        if (wcscmp(type,L"BUTTON")==0 && ((style&BS_TYPEMASK)==BS_PUSHBUTTON || (style&BS_TYPEMASK)==BS_DEFPUSHBUTTON))
            style=(style&~BS_TYPEMASK)|BS_OWNERDRAW;
        HWND handle=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance_,nullptr);
        // Themed checkboxes ignore the parent's dark text color; classic rendering
        // keeps native toggle/accessibility behavior while honoring WM_CTLCOLOR.
        if (handle && wcscmp(type,L"BUTTON")==0 && (style&BS_TYPEMASK)!=BS_OWNERDRAW) {
            using ThemeFunction=HRESULT(WINAPI*)(HWND,LPCWSTR,LPCWSTR);
            static HMODULE themes=LoadLibraryExW(L"uxtheme.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            static auto theme=themes ? reinterpret_cast<ThemeFunction>(GetProcAddress(themes,"SetWindowTheme")) : nullptr;
            if (theme) theme(handle,L"",L"");
        }
        return handle;
    }
    void place(HWND controlHandle,int x,int y,int w,int h) {
        MoveWindow(controlHandle,scale(MulDiv(x,clientWidth_,640)),scale(y),scale(MulDiv(w,clientWidth_,640)),scale(h),TRUE);
    }
    void makeFonts() {
        for (auto font : {normalFont_,titleFont_,rpmFont_}) if (font) DeleteObject(font);
        auto font=[this](int size,int weight) {return CreateFontW(-scale(size),0,0,0,weight,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");};
        normalFont_=font(14,FW_NORMAL);titleFont_=font(25,FW_SEMIBOLD);rpmFont_=font(27,FW_SEMIBOLD);
        for (auto handle : {title_,subtitle_,status_,group_[0],group_[1],rpm_[0],rpm_[1],actual_[0],actual_[1],
            temperature_[0],temperature_[1],target_[0],target_[1],slider_[0],slider_[1],source_,hint_,apply_,maximum_,restore_,footer_,updated_,exit_})
            SendMessageW(handle,WM_SETFONT,reinterpret_cast<WPARAM>(normalFont_),TRUE);
        SendMessageW(title_,WM_SETFONT,reinterpret_cast<WPARAM>(titleFont_),TRUE);
        for (auto handle : rpm_) SendMessageW(handle,WM_SETFONT,reinterpret_cast<WPARAM>(rpmFont_),TRUE);
        for (auto handle : curveControls_) SendMessageW(handle,WM_SETFONT,reinterpret_cast<WPARAM>(normalFont_),TRUE);
        for (auto handle : historyControls_) SendMessageW(handle,WM_SETFONT,reinterpret_cast<WPARAM>(normalFont_),TRUE);
        for (auto handle : osdControls_) SendMessageW(handle,WM_SETFONT,reinterpret_cast<WPARAM>(normalFont_),TRUE);
        for (auto handle : {tabs_,mode_,modeLabel_}) SendMessageW(handle,WM_SETFONT,reinterpret_cast<WPARAM>(normalFont_),TRUE);
    }
    void layout() {
        place(title_,24,18,592,36);place(subtitle_,24,57,592,22);place(status_,24,88,592,36);
        for (int index=0;index<2;++index) {
            const int x=24+index*308;
            place(group_[index],x,132,284,196);
            place(rpm_[index],x+16,160,252,37);place(actual_[index],x+16,201,252,22);
            place(temperature_[index],x+16,227,252,40);place(target_[index],x+16,270,252,22);
            place(slider_[index],x+12,295,260,25);
        }
        place(modeLabel_,24,340,150,24);place(mode_,180,338,436,130);
        place(source_,24,382,592,26);place(hint_,24,414,592,36);
        place(apply_,24,463,189,38);place(maximum_,225,463,189,38);place(restore_,426,463,190,38);
        place(footer_,24,518,592,40);place(updated_,24,clientHeight_-36,440,22);place(exit_,504,clientHeight_-43,112,32);
        place(tabs_,24,88,592,30);place(status_,24,clientHeight_-75,592,28);
        place(historyLabel_,24,134,130,24);place(historyPeriod_,160,130,456,160);
        place(historyNotice_,24,166,592,36);
        const int chartHeight=(clientHeight_-316)/2;
        place(historyWindows_[0],24,206,592,chartHeight);place(historyWindows_[1],24,218+chartHeight,592,chartHeight);
        place(editProfile_,24,136,592,130);
        for (int column=0;column<4;++column) place(curveHeaders_[column],24+column*148,188,142,40);
        for (int fan=0;fan<2;++fan) for (int point=0;point<5;++point) for (int field=0;field<2;++field)
            place(curveEdits_[fan][point][field],24+(fan*2+field)*148,236+point*42,138,30);
        place(saveCurves_,24,461,250,38);place(curveHint_,24,511,592,48);
        place(dashboard_,24,130,592,clientHeight_-218);
        place(osdTitle_,24,139,592,32);place(osdEnabled_,24,184,592,30);
        place(osdCornerLabel_,24,231,180,24);place(osdCorner_,210,227,406,140);
        for (int i=0;i<8;++i) place(osdChecks_[i],24+(i%2)*296,286+(i/2)*42,288,30);
        place(osdOpen_,24,474,180,36);place(osdNotice_,220,476,396,36);
        place(osdHelp_,24,521,592,42);
        InvalidateRect(window_,nullptr,TRUE);
    }
    void createControls() {
        title_=control(L"STATIC",L"FanController",0);
        subtitle_=control(L"STATIC",preview_ ? L"PRÉVIA · dados simulados · nenhum controle de hardware" : L"Dell G15 5530  ·  Ventilação e monitoramento",0);
        status_=control(L"STATIC",L"Conectando ao computador…",0);
        for (int index=0;index<2;++index) {
            group_[index]=control(L"BUTTON",index==0 ? L"Fan 50  ·  CPU provável" : L"Fan 51  ·  GPU",BS_GROUPBOX);
            rpm_[index]=control(L"STATIC",L"— RPM",0);
            actual_[index]=control(L"STATIC",L"Boost atual: —",0);
            temperature_[index]=control(L"STATIC",L"Temperatura indisponível",0);
            target_[index]=control(L"STATIC",L"Boost solicitado: 30 / 100",0);
            slider_[index]=control(TRACKBAR_CLASSW,index==0 ? L"Boost fan 50" : L"Boost fan 51",WS_TABSTOP|TBS_AUTOTICKS,200+index);
            SendMessageW(slider_[index],TBM_SETRANGE,TRUE,MAKELPARAM(0,100));
            SendMessageW(slider_[index],TBM_SETTICFREQ,25,0);SendMessageW(slider_[index],TBM_SETPOS,TRUE,30);
        }
        source_=control(L"BUTTON",L"Temperatura GPU pela NVIDIA (NVML)",WS_TABSTOP|BS_AUTOCHECKBOX,sourceId);
        hint_=control(L"STATIC",L"Opcional. A NVML pode interferir no repouso da GPU.",0);
        apply_=control(L"BUTTON",L"Aplicar",WS_TABSTOP|BS_DEFPUSHBUTTON,applyId);
        maximum_=control(L"BUTTON",L"Máximo (100)",WS_TABSTOP|BS_PUSHBUTTON,maximumId);
        restore_=control(L"BUTTON",L"Restaurar",WS_TABSTOP|BS_PUSHBUTTON,restoreId);
        footer_=control(L"STATIC",L"Fechar a janela mantém o programa na bandeja.\nSair tenta restaurar o perfil e os boosts anteriores.",0);
        updated_=control(L"STATIC",L"Aguardando a primeira leitura",0);
        exit_=control(L"BUTTON",L"Sair",WS_TABSTOP|BS_PUSHBUTTON,exitId);
        mainControls_={group_[0],group_[1],rpm_[0],rpm_[1],actual_[0],actual_[1],temperature_[0],temperature_[1],
            target_[0],target_[1],slider_[0],slider_[1],source_,hint_,apply_,maximum_,restore_,footer_};
        tabs_=control(WC_TABCONTROLW,L"Abas",WS_TABSTOP|TCS_OWNERDRAWFIXED);
        SetWindowSubclass(tabs_,tabsProc,1,reinterpret_cast<DWORD_PTR>(this));
        TCITEMW tab{};tab.mask=TCIF_TEXT;tab.pszText=const_cast<wchar_t*>(L"Controle");TabCtrl_InsertItem(tabs_,0,&tab);
        tab.pszText=const_cast<wchar_t*>(L"Curvas");TabCtrl_InsertItem(tabs_,1,&tab);
        tab.pszText=const_cast<wchar_t*>(L"Histórico");TabCtrl_InsertItem(tabs_,2,&tab);
        tab.pszText=const_cast<wchar_t*>(L"Monitoramento");TabCtrl_InsertItem(tabs_,3,&tab);
        tab.pszText=const_cast<wchar_t*>(L"OSD");TabCtrl_InsertItem(tabs_,4,&tab);
        TabCtrl_SetCurSel(tabs_,3);
        dashboard_=control(L"STATIC",L"Monitoramento ao vivo",0);
        SetWindowSubclass(dashboard_,dashboardProc,1,reinterpret_cast<DWORD_PTR>(this));
        dashboardControls_.push_back(dashboard_);
        osdTitle_=control(L"STATIC",L"Exibição na tela · RTSS",0);
        osdEnabled_=control(L"BUTTON",L"Mostrar métricas no jogo",WS_TABSTOP|BS_AUTOCHECKBOX,osdEnableId);
        osdCornerLabel_=control(L"STATIC",L"Posição no jogo",0);
        osdCorner_=control(L"COMBOBOX",L"Posição do OSD",WS_TABSTOP|CBS_DROPDOWNLIST,osdCornerId);
        for (const auto* name : {L"Superior esquerdo",L"Superior direito",L"Inferior esquerdo",L"Inferior direito"})
            SendMessageW(osdCorner_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        const wchar_t* metrics[]{L"Temperatura CPU",L"Uso CPU",L"Temperatura GPU",L"Uso GPU",L"Rotação fan CPU (RPM)",L"Rotação fan GPU (RPM)",L"FPS",L"Tempo de quadro (ms)"};
        osdControls_={osdTitle_,osdEnabled_,osdCornerLabel_,osdCorner_};
        for (int i=0;i<8;++i) {osdChecks_[i]=control(L"BUTTON",metrics[i],WS_TABSTOP|BS_AUTOCHECKBOX,osdMetricId+i);osdControls_.push_back(osdChecks_[i]);}
        osdOpen_=control(L"BUTTON",L"Abrir RTSS",WS_TABSTOP|BS_PUSHBUTTON,openRtssId);
        osdNotice_=control(L"STATIC",L"Verificando RTSS…",0);
        osdHelp_=control(L"STATIC",L"O RTSS precisa estar aberto e detectar o jogo.\nPreferências salvas por usuário · controle das fans independente.",0);
        for (auto handle : {osdOpen_,osdNotice_,osdHelp_}) osdControls_.push_back(handle);
        try {osdSettings_=fanctl::loadOsdSettings();} catch (const std::exception& error) {osdError_=wide(error.what());}
        SendMessageW(osdEnabled_,BM_SETCHECK,osdSettings_.enabled ? BST_CHECKED : BST_UNCHECKED,0);
        SendMessageW(osdCorner_,CB_SETCURSEL,osdSettings_.corner,0);
        for (int i=0;i<8;++i) SendMessageW(osdChecks_[i],BM_SETCHECK,(osdSettings_.metrics&(1u<<i)) ? BST_CHECKED : BST_UNCHECKED,0);
        historyLabel_=control(L"STATIC",L"Período:",0);
        historyPeriod_=control(L"COMBOBOX",L"Período do histórico",WS_TABSTOP|CBS_DROPDOWNLIST,historyPeriodId);
        for (const auto* name : {L"Última hora",L"Últimas 24 horas",L"Últimos 7 dias",L"Últimos 30 dias",L"Últimos 90 dias"})
            SendMessageW(historyPeriod_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        SendMessageW(historyPeriod_,CB_SETCURSEL,1,0);
        historyNotice_=control(L"STATIC",L"Carregando histórico…",0);
        historyControls_={historyLabel_,historyPeriod_,historyNotice_};
        for (int fan=0;fan<2;++fan) {
            historyWindows_[fan]=historyCharts_[fan].create(window_,instance_,fan);
            historyControls_.push_back(historyWindows_[fan]);
        }
        if (!preview_) history_=std::make_unique<fanctl::HistoryService>([this] {PostMessageW(window_,historyMessage,0,0);});
        mode_=control(L"COMBOBOX",L"Modo de controle",WS_TABSTOP|CBS_DROPDOWNLIST,modeId);
        for (auto name : {L"Manual",L"Normal",L"Performance"}) SendMessageW(mode_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        SendMessageW(mode_,CB_SETCURSEL,0,0);mainControls_.push_back(mode_);
        modeLabel_=control(L"STATIC",L"Modo de controle:",0);mainControls_.push_back(modeLabel_);
        editProfile_=control(L"COMBOBOX",L"Perfil para editar",WS_TABSTOP|CBS_DROPDOWNLIST,editProfileId);
        for (auto name : {L"Normal",L"Performance"}) SendMessageW(editProfile_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        SendMessageW(editProfile_,CB_SETCURSEL,0,0);curveControls_.push_back(editProfile_);
        const wchar_t* headers[]{L"CPU provável · °C",L"Fan 50 · boost",L"GPU NVML · °C",L"Fan 51 · boost"};
        for (int column=0;column<4;++column) {curveHeaders_[column]=control(L"STATIC",headers[column],0);curveControls_.push_back(curveHeaders_[column]);}
        for (int fan=0;fan<2;++fan) for (int point=0;point<5;++point) for (int field=0;field<2;++field) {
            auto& edit=curveEdits_[fan][point][field];
            edit=control(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_NUMBER|ES_AUTOHSCROLL);
            SendMessageW(edit,EM_SETLIMITTEXT,3,0);curveControls_.push_back(edit);
        }
        saveCurves_=control(L"BUTTON",L"Salvar e aplicar",WS_TABSTOP|BS_PUSHBUTTON,saveCurvesId);
        curveHint_=control(L"STATIC",L"Normal: 20 a 100. Performance: 30 a 100. Boosts não decrescentes.\nAlterações só entram em vigor ao salvar e aplicar.",0);
        curveControls_.push_back(saveCurves_);curveControls_.push_back(curveHint_);
        try {curves_=loadCurves();} catch (const std::exception& error) {settingsError_=wide(error.what());}
        try {
            if (const auto saved=fanctl::readPersistent(L"fanctl-startup.txt")) startup_=fanctl::decodeStartup(*saved);
            SendMessageW(mode_,CB_SETCURSEL,startup_.mode,0);
            SendMessageW(slider_[0],TBM_SETPOS,TRUE,startup_.first);SendMessageW(slider_[1],TBM_SETPOS,TRUE,startup_.second);
        } catch (const std::exception& error) {settingsError_+=L" "+wide(error.what());}
        if (!settingsError_.empty() && startupLaunch_) ShowWindow(window_,SW_SHOW);
        fillCurveEditor();
        makeFonts();layout();
        selectTab();
        taskbarCreated_=RegisterWindowMessageW(L"TaskbarCreated");
        addTray();render();
    }
    void addTray() {
        tray_={};tray_.cbSize=sizeof(tray_);tray_.hWnd=window_;tray_.uID=1;
        tray_.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray_.uCallbackMessage=trayMessage;tray_.hIcon=icon_;
        wcscpy_s(tray_.szTip,L"FanController — monitorando");
        trayAdded_=Shell_NotifyIconW(NIM_ADD,&tray_)!=FALSE;
        if (trayAdded_) {tray_.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&tray_);}
    }
    void show() {ShowWindow(window_,SW_RESTORE);SetForegroundWindow(window_);}
    void hide() {
        if (trayAdded_) ShowWindow(window_,SW_HIDE);
        else MessageBoxW(window_,L"O ícone da bandeja não está disponível. Use Sair para encerrar com restauração.",L"FanController",MB_OK|MB_ICONINFORMATION);
    }
    static std::wstring metricText(const std::optional<double>& value,const wchar_t* unit,int decimals=0) {
        if (!value || !std::isfinite(*value)) return L"indisponível";
        wchar_t text[80]{};swprintf_s(text,L"%.*f %s",decimals,*value,unit);return text;
    }
    void refreshMonitoring(bool record=true) {
        fanctl::OsdValues values;
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
                gpuUsageFromWindows_=snapshot.gpuUsageFromWindows;
                values.cpuRpm=snapshot.cpuRpm;values.gpuRpm=snapshot.gpuRpm;
            }
            try {
                const auto game=rtss_->readGame();values.fps=game.fps;values.frameTimeMs=game.frameTimeMs;
                if (osdSettings_.enabled) SetWindowTextW(osdNotice_,fanctl::rtssStatusText(rtss_->publish(fanctl::formatOsd(values,osdSettings_))));
                else {rtss_->release();SetWindowTextW(osdNotice_,L"OSD desligado");}
            } catch (const std::exception&) {SetWindowTextW(osdNotice_,L"OSD indisponível · controle das fans preservado");}
        }
        if (preview_) SetWindowTextW(osdNotice_,L"Prévia: nenhuma publicação no RTSS");
        if (preview_) SetWindowTextW(updated_,L"Prévia ao vivo · dados simulados");
        if (!osdError_.empty()) SetWindowTextW(osdNotice_,osdError_.c_str());
        liveValues_=values;if (record) {recent_.push_back(values);if (recent_.size()>120) recent_.pop_front();}
        InvalidateRect(dashboard_,nullptr,FALSE);
    }
    void saveOsd() {
        if (preview_) return;
        fanctl::OsdSettings settings;
        settings.enabled=SendMessageW(osdEnabled_,BM_GETCHECK,0,0)==BST_CHECKED;
        settings.corner=static_cast<int>(SendMessageW(osdCorner_,CB_GETCURSEL,0,0));settings.metrics=0;
        for (int i=0;i<8;++i) if (SendMessageW(osdChecks_[i],BM_GETCHECK,0,0)==BST_CHECKED) settings.metrics|=1u<<i;
        try {fanctl::saveOsdSettings(settings);osdSettings_=settings;osdError_.clear();}
        catch (const std::exception& error) {
            osdError_=L"Preferências não salvas: "+wide(error.what());
            SendMessageW(osdEnabled_,BM_SETCHECK,osdSettings_.enabled ? BST_CHECKED : BST_UNCHECKED,0);
            SendMessageW(osdCorner_,CB_SETCURSEL,osdSettings_.corner,0);
            for (int i=0;i<8;++i) SendMessageW(osdChecks_[i],BM_SETCHECK,(osdSettings_.metrics&(1u<<i)) ? BST_CHECKED : BST_UNCHECKED,0);
        }
        refreshMonitoring(false);
    }
    void drawDashboard(HDC dc,const RECT& bounds) {
        FillRect(dc,&bounds,background_);SetBkMode(dc,TRANSPARENT);
        const int width=MulDiv(bounds.right,96,dpi_),height=MulDiv(bounds.bottom,96,dpi_);
        auto label=[&](int x,int y,const std::wstring& text,COLORREF color,HFONT font) {
            SelectObject(dc,font);SetTextColor(dc,color);TextOutW(dc,scale(x),scale(y),text.c_str(),static_cast<int>(text.size()));
        };
        const COLORREF white=RGB(231,237,245),muted=RGB(151,166,185),cpu=RGB(74,211,191),gpu=RGB(130,165,255);
        auto box=[&](int x,int y,int w,int h) {
            RECT rect{scale(x),scale(y),scale(x+w),scale(y+h)};
            HBRUSH brush=CreateSolidBrush(RGB(28,35,46));FillRect(dc,&rect,brush);DeleteObject(brush);
        };
        const int cardWidth=(width-16)/2;
        for (int fan=0;fan<2;++fan) {
            const int x=fan*(cardWidth+16);box(x,0,cardWidth,150);
            label(x+16,12,fan ? L"GPU · temperatura NVML" : L"CPU · sensor Dell provável",fan ? gpu : cpu,normalFont_);
            label(x+16,40,metricText(fan ? liveValues_.gpuTemperature : liveValues_.cpuTemperature,L"°C"),white,rpmFont_);
            const auto usage=fan ? liveValues_.gpuUsage : liveValues_.cpuUsage;
            const std::wstring source=!usage ? L"" : preview_ ? L" · simulado" : fan && !gpuUsageFromWindows_ ? L" · NVML" : L" · Windows";
            label(x+16,84,L"Uso  "+metricText(usage,L"%")+source,white,normalFont_);
            label(x+16,113,L"Fan  "+metricText(fan ? liveValues_.gpuRpm : liveValues_.cpuRpm,L"RPM"),muted,normalFont_);
        }
        box(0,166,width,82);label(16,178,L"JOGO · aplicativo em primeiro plano detectado pelo RTSS",muted,normalFont_);
        label(16,207,metricText(liveValues_.fps,L"FPS",1),white,titleFont_);
        label(width/2,211,L"Quadro  "+metricText(liveValues_.frameTimeMs,L"ms",2),white,normalFont_);
        label(0,267,L"ÚLTIMOS 120 S · temperatura (linha) e uso (tracejado)",muted,normalFont_);
        const int chartTop=302,chartBottom=std::max(chartTop+42,height-45);
        for (int fan=0;fan<2;++fan) {
            const int x=fan*(cardWidth+16);box(x,chartTop,cardWidth,chartBottom-chartTop);
            label(x+10,chartTop+8,fan ? L"GPU · 0–125 °C / 0–100 %" : L"CPU · 0–125 °C / 0–100 %",fan ? gpu : cpu,normalFont_);
            const int left=x+10,right=x+cardWidth-10,top=chartTop+35,bottom=chartBottom-10;
            HPEN grid=CreatePen(PS_SOLID,1,RGB(46,57,73));auto oldPen=SelectObject(dc,grid);
            for (int tick=0;tick<=4;++tick) {const int y=top+(bottom-top)*tick/4;MoveToEx(dc,scale(left),scale(y),nullptr);LineTo(dc,scale(right),scale(y));}
            SelectObject(dc,oldPen);DeleteObject(grid);
            for (int series=0;series<2;++series) {
                HPEN pen=CreatePen(series ? PS_DOT : PS_SOLID,1,fan ? gpu : cpu);oldPen=SelectObject(dc,pen);
                bool connected=false;int index=0;
                for (const auto& entry : recent_) {
                    const auto value=fan ? (series ? entry.gpuUsage : entry.gpuTemperature) : (series ? entry.cpuUsage : entry.cpuTemperature);
                    const int px=left+(right-left)*(120-static_cast<int>(recent_.size())+index++)/119;
                    if (!value) {connected=false;continue;}
                    const double maximum=series ? 100.0 : 125.0;
                    const int py=bottom-static_cast<int>((bottom-top)*std::clamp(*value,0.0,maximum)/maximum);
                    if (connected) LineTo(dc,scale(px),scale(py));else MoveToEx(dc,scale(px),scale(py),nullptr);
                    connected=true;
                }
                SelectObject(dc,oldPen);DeleteObject(pen);
            }
        }
        label(0,height-28,preview_ ? L"Dados de demonstração · sem acesso ao hardware" : L"Ao vivo · 1 s · lacunas indicam leituras indisponíveis",muted,normalFont_);
    }
    static LRESULT CALLBACK dashboardProc(HWND window,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR data) {
        auto* app=reinterpret_cast<Application*>(data);
        if (message==WM_ERASEBKGND) return 1;
        if (message==WM_PAINT) {
            PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT bounds{};GetClientRect(window,&bounds);
            HDC buffer=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,std::max(1L,bounds.right),std::max(1L,bounds.bottom));
            auto old=SelectObject(buffer,bitmap);app->drawDashboard(buffer,bounds);BitBlt(dc,0,0,bounds.right,bounds.bottom,buffer,0,0,SRCCOPY);
            SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(window,&paint);return 0;
        }
        return DefSubclassProc(window,message,wparam,lparam);
    }
    static LRESULT CALLBACK tabsProc(HWND window,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR data) {
        auto* app=reinterpret_cast<Application*>(data);
        if (message==WM_ERASEBKGND) return 1;
        if (message==WM_PAINT) {
            PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT bounds{};GetClientRect(window,&bounds);
            FillRect(dc,&bounds,app->background_);SetBkMode(dc,TRANSPARENT);SelectObject(dc,app->normalFont_);
            const int selected=TabCtrl_GetCurSel(window);
            for (int index=0;index<TabCtrl_GetItemCount(window);++index) {
                RECT rect{};TabCtrl_GetItemRect(window,index,&rect);
                const bool active=index==selected;
                HBRUSH brush=CreateSolidBrush(active ? RGB(41,63,73) : RGB(28,35,46));FillRect(dc,&rect,brush);DeleteObject(brush);
                wchar_t text[64]{};TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=text;item.cchTextMax=64;TabCtrl_GetItem(window,index,&item);
                SetTextColor(dc,active ? RGB(91,230,205) : RGB(190,202,220));DrawTextW(dc,text,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                if (active) {
                    RECT line{rect.left,rect.bottom-app->scale(2),rect.right,rect.bottom};
                    brush=CreateSolidBrush(RGB(74,211,191));FillRect(dc,&line,brush);DeleteObject(brush);
                    if (GetFocus()==window) {InflateRect(&rect,-3,-3);DrawFocusRect(dc,&rect);}
                }
            }
            EndPaint(window,&paint);return 0;
        }
        const auto result=DefSubclassProc(window,message,wparam,lparam);
        if (message==WM_SETFOCUS || message==WM_KILLFOCUS || message==WM_LBUTTONUP || message==WM_KEYUP) InvalidateRect(window,nullptr,FALSE);
        return result;
    }
    View current() {std::lock_guard<std::mutex> lock(mutex_);return view_;}
    void render() {
        const auto view=current();
        const std::wstring status=preview_ ? L"Dados simulados · nenhum ajuste de hardware" : !view.error.empty() ? L"Atenção: "+view.error :
            view.busy ? L"Processando…" : view.active ? (view.mode==1 ? L"Normal ativo · perfil Custom" :
                view.mode==2 ? L"Performance ativo · perfil Custom" : L"Controle manual ativo · perfil Custom") : L"Monitorando · nenhum ajuste manual ativo";
        SetWindowTextW(status_,status.c_str());
        if (view.frame) {
            for (int index=0;index<2;++index) {
                const auto& fan=fanctl::findFan(view.frame->snapshot,static_cast<std::uint32_t>(50+index));
                SetWindowTextW(rpm_[index],(std::to_wstring(fan.rpm)+L" RPM").c_str());
                SetWindowTextW(actual_[index],(L"Boost atual: "+std::to_wstring(fan.boost)).c_str());
            }
            std::wstring cpu=L"CPU provável: indisponível";
            for (const auto& sensor : view.frame->snapshot.sensors)
                if (sensor.id==1) cpu=L"CPU provável: "+std::to_wstring(sensor.temperature)+L" °C · Dell";
            SetWindowTextW(temperature_[0],cpu.c_str());
            const auto& gpu=view.frame->gpu;
            const auto gpuText=(gpu.temperature ? L"GPU: "+std::to_wstring(*gpu.temperature)+L" °C" : L"GPU: indisponível")+
                (gpu.source==fanctl::GpuSource::Nvml ? std::wstring(L" · NVML") : std::wstring(L" · Dell\nLeitura não validada pela NVIDIA"));
            SetWindowTextW(temperature_[1],gpuText.c_str());
            SYSTEMTIME time{};GetLocalTime(&time);wchar_t stamp[64]{};
            swprintf_s(stamp,L"Atualizado às %02u:%02u:%02u",time.wHour,time.wMinute,time.wSecond);
            SetWindowTextW(updated_,stamp);
        } else {
            for (int index=0;index<2;++index) {
                SetWindowTextW(rpm_[index],L"— RPM");SetWindowTextW(actual_[index],L"Boost atual: —");
                SetWindowTextW(temperature_[index],L"Temperatura indisponível");
            }
        }
        const bool sourceReady=view.frame && (!view.nvml || view.frame->gpu.temperature.has_value());
        const bool canApply=view.ready && sourceReady && !view.pending && !view.busy;
        EnableWindow(apply_,canApply);EnableWindow(maximum_,canApply);
        EnableWindow(restore_,view.active && !view.busy);SetWindowTextW(restore_,view.pending ? L"Tentar restaurar" : L"Restaurar");
        EnableWindow(source_,view.ready && !view.active && !view.busy);
        EnableWindow(mode_,!view.busy && !view.pending);EnableWindow(saveCurves_,!view.busy && !view.pending);
        const bool manual=SendMessageW(mode_,CB_GETCURSEL,0,0)==0;
        SetWindowTextW(hint_,manual ? L"Opcional. A NVML pode interferir no repouso da GPU." :
            L"NVML ativa nos perfis. Piso: Normal 20; Performance 30. GPU pode permanecer acordada.");
        if (view.active && view.mode && view.frame) {
            for (int index=0;index<2;++index) SetWindowTextW(target_[index],
                (L"Boost da curva: "+std::to_wstring(fanctl::findFan(view.frame->snapshot,50+index).boost)+L" / 100").c_str());
        } else targets();
        for (auto slider : slider_) EnableWindow(slider,manual && !view.busy);
        EnableWindow(source_,view.ready && !view.active && !view.busy && manual);
        if (!settingsError_.empty()) SetWindowTextW(status_,settingsError_.c_str());
        SendMessageW(source_,BM_SETCHECK,view.nvml ? BST_CHECKED : BST_UNCHECKED,0);
        EnableWindow(exit_,!view.busy);
        if (trayAdded_) {
            const auto tip=(view.pending ? std::wstring(L"FanController — restauração pendente") :
                view.active ? std::wstring(L"FanController — controle ativo") : std::wstring(L"FanController — monitorando"));
            wcscpy_s(tray_.szTip,tip.c_str());tray_.uFlags=NIF_TIP;Shell_NotifyIconW(NIM_MODIFY,&tray_);
        }
        if (view.attention) {show();std::lock_guard<std::mutex> lock(mutex_);view_.attention=false;}
    }
    void send(Request request) {
        if (preview_) {if (request.action==Action::Exit) DestroyWindow(window_);return;}
        {std::lock_guard<std::mutex> lock(mutex_);
            requests_.push_back(request);view_.busy=true;
            if (request.action==Action::Restore || request.action==Action::Exit) cancel_=true;
        }
        wake_.notify_one();render();
    }
    void targets() {
        for (int index=0;index<2;++index) {
            const auto value=static_cast<int>(SendMessageW(slider_[index],TBM_GETPOS,0,0));
            SetWindowTextW(target_[index],(L"Boost solicitado: "+std::to_wstring(value)+L" / 100").c_str());
        }
    }
    void selectTab() {
        InvalidateRect(tabs_,nullptr,FALSE);
        const int selected=TabCtrl_GetCurSel(tabs_);
        for (auto handle : mainControls_) ShowWindow(handle,selected==0 ? SW_SHOW : SW_HIDE);
        for (auto handle : curveControls_) ShowWindow(handle,selected==1 ? SW_SHOW : SW_HIDE);
        for (auto handle : historyControls_) ShowWindow(handle,selected==2 ? SW_SHOW : SW_HIDE);
        for (auto handle : dashboardControls_) ShowWindow(handle,selected==3 ? SW_SHOW : SW_HIDE);
        for (auto handle : osdControls_) ShowWindow(handle,selected==4 ? SW_SHOW : SW_HIDE);
        if (selected==2) refreshHistoryPeriod();
    }
    void refreshHistoryPeriod() {
        const fanctl::HistoryTime periods[]{3600,86400,7*86400,30*86400,90*86400};
        const auto selected=SendMessageW(historyPeriod_,CB_GETCURSEL,0,0);
        if (history_ && selected>=0 && selected<5) history_->period(periods[selected]);
        renderHistory();
    }
    void renderHistory() {
        if (!history_) {SetWindowTextW(historyNotice_,L"Prévia · histórico não acessado");return;}
        const auto history=history_->view();
        std::wstring notice=history.error.empty() ? (history.loading ? L"Carregando histórico…" :
            L"Coleta a cada 5 min · retenção de 90 dias · lacunas indicam dados ausentes") : wide(history.error);
        SetWindowTextW(historyNotice_,notice.c_str());
        for (auto& chart : historyCharts_) chart.update(history);
    }
    void fillCurveEditor() {
        for (int fan=0;fan<2;++fan) for (int point=0;point<5;++point) {
            const auto& value=curves_[editingProfile_][fan][point];
            SetWindowTextW(curveEdits_[fan][point][0],std::to_wstring(value.temperature).c_str());
            SetWindowTextW(curveEdits_[fan][point][1],std::to_wstring(value.boost).c_str());
        }
    }
    fanctl::CurveSettings editedCurves() {
        auto settings=curves_;
        for (int fan=0;fan<2;++fan) for (int point=0;point<5;++point) for (int field=0;field<2;++field) {
            wchar_t text[16]{};GetWindowTextW(curveEdits_[fan][point][field],text,16);
            if (!*text) throw std::runtime_error("Preencha todos os pontos da curva.");
            for (const auto* digit=text;*digit;++digit) if (*digit<L'0' || *digit>L'9')
                throw std::runtime_error("Use apenas numeros inteiros nos pontos.");
            const int value=_wtoi(text);
            auto& entry=settings[editingProfile_][fan][point];
            if (field==0) entry.temperature=value;else entry.boost=value;
        }
        fanctl::validateCurves(settings);return settings;
    }
    void saveEditor() {
        if (preview_) return;
        try {
            const auto settings=editedCurves();saveCurves(settings);curves_=settings;settingsError_.clear();
            Request request{Action::Curves};request.curves=settings;send(request);
        } catch (const std::exception& error) {
            MessageBoxW(window_,wide(error.what()).c_str(),L"Curvas não salvas",MB_OK|MB_ICONERROR);
        }
    }
    void apply(bool maximum) {
        if (maximum) SendMessageW(mode_,CB_SETCURSEL,0,0);
        if (maximum) for (auto slider : slider_) SendMessageW(slider,TBM_SETPOS,TRUE,100);
        targets();
        Request request{Action::Apply,static_cast<int>(SendMessageW(slider_[0],TBM_GETPOS,0,0)),static_cast<int>(SendMessageW(slider_[1],TBM_GETPOS,0,0))};
        request.mode=static_cast<int>(SendMessageW(mode_,CB_GETCURSEL,0,0));
        if (request.mode && !settingsError_.empty()) {
            MessageBoxW(window_,L"Revise as curvas e use Salvar e aplicar antes de ativar um perfil automático.",L"FanController",MB_OK|MB_ICONERROR);return;
        }
        request.curves=curves_;send(request);
    }
    void menu() {
        HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,openMenu,L"Abrir FanController");
        AppendMenuW(menu,MF_STRING|(osdSettings_.enabled ? MF_CHECKED : 0),osdMenu,L"Mostrar OSD no jogo");
        AppendMenuW(menu,MF_STRING,restoreMenu,L"Restaurar ajustes anteriores");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,exitMenu,L"Sair e restaurar");
        const auto view=current();EnableMenuItem(menu,restoreMenu,MF_BYCOMMAND|(view.active && !view.busy ? MF_ENABLED : MF_GRAYED));
        POINT point{};GetCursorPos(&point);SetForegroundWindow(window_);
        const auto selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,point.x,point.y,0,window_,nullptr);
        DestroyMenu(menu);PostMessageW(window_,WM_NULL,0,0);
        if (selected==openMenu) show();
        else if (selected==osdMenu) {SendMessageW(osdEnabled_,BM_SETCHECK,osdSettings_.enabled ? BST_UNCHECKED : BST_CHECKED,0);saveOsd();}
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
                std::optional<fanctl::ControlFrame> frame;
                std::optional<fanctl::ControlFrame> partialHistoryFrame;
                const auto readAttempt=backend.readAttempt();
                try {
                    if (request) {
                        if (request->action==Action::Exit || request->action==Action::Restore) {
                            if (restore()) {
                                error.clear();
                                if (request->action==Action::Exit) {PostMessageW(window_,exitMessage,0,0);return;}
                            } else attention=true;
                            cancel_=false;
                        } else if (request->action==Action::Source) {
                            if (session) throw std::runtime_error("Restaure os ajustes antes de mudar a fonte GPU.");
                            nvml=manualNvml=request->nvml;makeMonitor();error.clear();transientError=false;
                        } else if (request->action==Action::Curves) {
                            settings=request->curves;
                            if (automatic) automatic->replaceCurves(settings[mode-1]);
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
                } catch (const std::exception& exception) {
                    // Keep telemetry obtained before a failed query; never reuse an older snapshot.
                    if (backend.readAttempt()!=readAttempt) {
                        auto partial=backend.partialSnapshot();
                        partialHistoryFrame=fanctl::ControlFrame{partial,fanctl::selectedGpu(partial,*monitor)};
                    }
                    error=wide(exception.what());attention=true;transientError=!session;
                    if (session && !restore()) attention=true;
                    if (!session) {automatic.reset();mode=0;if (nvml!=manualNvml) {nvml=manualNvml;makeMonitor();}}
                    if (request && request->action==Action::Apply && request->startupAttempt>=0 &&
                        request->startupAttempt<11 && !session && !pending && !cancel_) {
                        retry=*request;++retry->startupAttempt;retryAt=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                        error=L"Aguardando a interface térmica para retomar o perfil…";attention=false;transientError=false;
                    }
                }
                history_->offer(frame ? frame : partialHistoryFrame,nvml ? fanctl::GpuSource::Nvml : fanctl::GpuSource::Dell);
                if (monitoring_ && frame) monitoring_->submitThermal(*frame);
                {std::lock_guard<std::mutex> lock(mutex_);
                    view_.frame=std::move(frame);view_.error=error;view_.active=session!=nullptr;view_.pending=pending;
                    view_.nvml=nvml;view_.ready=true;view_.busy=!requests_.empty();view_.attention=attention;
                    view_.mode=mode;
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

    static LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
        Application* app=reinterpret_cast<Application*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if (message==WM_NCCREATE) {
            app=static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            app->window_=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));
        }
        if (!app) return DefWindowProcW(window,message,wparam,lparam);
        if (app->taskbarCreated_ && message==app->taskbarCreated_) {app->addTray();return 0;}
        switch (message) {
        case WM_CREATE:app->createControls();return 0;
        case WM_CLOSE:if (app->preview_) DestroyWindow(window);else app->hide();return 0;
        case WM_TIMER:if (wparam==1) app->refreshMonitoring();return 0;
        case WM_GETMINMAXINFO: {
            auto* limits=reinterpret_cast<MINMAXINFO*>(lparam);
            RECT rect{0,0,app->scale(640),app->scale(640)};
            AdjustWindowRectExForDpi(&rect,WS_OVERLAPPEDWINDOW,FALSE,0,app->dpi_);
            limits->ptMinTrackSize={rect.right-rect.left,rect.bottom-rect.top};return 0;
        }
        case WM_QUERYENDSESSION: {
            app->send({Action::Restore});
            const auto until=GetTickCount64()+4000;
            while (app->current().busy && GetTickCount64()<until) Sleep(20);
            return TRUE;
        }
        case WM_ENDSESSION:if (wparam) app->send({Action::Exit});return 0;
        case WM_SIZE:
            if (wparam==SIZE_MINIMIZED && app->trayAdded_) app->hide();
            else {app->clientWidth_=MulDiv(LOWORD(lparam),96,app->dpi_);app->clientHeight_=MulDiv(HIWORD(lparam),96,app->dpi_);if (app->tabs_) app->layout();}
            return 0;
        case WM_DPICHANGED: {
            app->dpi_=HIWORD(wparam);const auto* rect=reinterpret_cast<RECT*>(lparam);
            SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);
            app->makeFonts();app->layout();return 0;
        }
        case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX: {
            HDC dc=reinterpret_cast<HDC>(wparam);SetBkColor(dc,RGB(19,24,32));SetTextColor(dc,RGB(231,237,245));
            return reinterpret_cast<LRESULT>(app->background_);
        }
        case WM_DRAWITEM: {
            const auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
            if (item->CtlType==ODT_BUTTON) {
                const bool disabled=(item->itemState&ODS_DISABLED)!=0,pressed=(item->itemState&ODS_SELECTED)!=0;
                HBRUSH brush=CreateSolidBrush(pressed ? RGB(30,76,76) : RGB(36,48,61));FillRect(item->hDC,&item->rcItem,brush);DeleteObject(brush);
                HBRUSH border=CreateSolidBrush(disabled ? RGB(42,49,60) : RGB(63,106,111));FrameRect(item->hDC,&item->rcItem,border);DeleteObject(border);
                wchar_t text[160]{};GetWindowTextW(item->hwndItem,text,160);SetBkMode(item->hDC,TRANSPARENT);
                SetTextColor(item->hDC,disabled ? RGB(106,118,134) : RGB(229,241,244));SelectObject(item->hDC,app->normalFont_);
                RECT rect=item->rcItem;DrawTextW(item->hDC,text,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                if (item->itemState&ODS_FOCUS) {InflateRect(&rect,-4,-4);DrawFocusRect(item->hDC,&rect);}return TRUE;
            }
            if (item->hwndItem==app->tabs_) {
                const bool selected=static_cast<int>(item->itemID)==TabCtrl_GetCurSel(app->tabs_);
                HBRUSH brush=CreateSolidBrush(selected ? RGB(41,63,73) : RGB(28,35,46));FillRect(item->hDC,&item->rcItem,brush);DeleteObject(brush);
                wchar_t text[64]{};TCITEMW tab{};tab.mask=TCIF_TEXT;tab.pszText=text;tab.cchTextMax=64;TabCtrl_GetItem(app->tabs_,item->itemID,&tab);
                SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,selected ? RGB(91,230,205) : RGB(190,202,220));
                SelectObject(item->hDC,app->normalFont_);RECT rect=item->rcItem;DrawTextW(item->hDC,text,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);return TRUE;
            }
            break;
        }
        case WM_HSCROLL:app->targets();return 0;
        case WM_NOTIFY:
            if (reinterpret_cast<NMHDR*>(lparam)->hwndFrom==app->tabs_ && reinterpret_cast<NMHDR*>(lparam)->code==TCN_SELCHANGE)
                app->selectTab();
            return 0;
        case WM_COMMAND:
            if (LOWORD(wparam)>=osdMetricId && LOWORD(wparam)<osdMetricId+8) {if (HIWORD(wparam)==BN_CLICKED) app->saveOsd();return 0;}
            switch (LOWORD(wparam)) {
            case osdEnableId:app->saveOsd();break;
            case osdCornerId:if (HIWORD(wparam)==CBN_SELCHANGE) app->saveOsd();break;
            case openRtssId:if (!app->preview_ && !fanctl::openRtss()) MessageBoxW(window,L"Não foi possível abrir o RTSS instalado.",L"FanController",MB_OK|MB_ICONINFORMATION);break;
            case applyId:app->apply(false);break;
            case maximumId:app->apply(true);break;
            case restoreId:app->send({Action::Restore});break;
            case sourceId:app->send({Action::Source,30,30,SendMessageW(app->source_,BM_GETCHECK,0,0)==BST_CHECKED});break;
            case exitId:if (app->preview_) DestroyWindow(window);else app->send({Action::Exit});break;
            case modeId:if (HIWORD(wparam)==CBN_SELCHANGE) app->render();break;
            case historyPeriodId:if (HIWORD(wparam)==CBN_SELCHANGE) app->refreshHistoryPeriod();break;
            case saveCurvesId:app->saveEditor();break;
            case editProfileId:
                if (HIWORD(wparam)==CBN_SELCHANGE) {
                    bool changed=true;
                    try {changed=fanctl::encodeCurves(app->editedCurves())!=fanctl::encodeCurves(app->curves_);} catch (const std::exception&) {}
                    if (changed && MessageBoxW(window,L"Descartar as alterações não salvas deste perfil?",L"Trocar perfil de edição",MB_YESNO|MB_ICONQUESTION)!=IDYES) {
                        SendMessageW(app->editProfile_,CB_SETCURSEL,app->editingProfile_,0);break;
                    }
                    app->editingProfile_=static_cast<int>(SendMessageW(app->editProfile_,CB_GETCURSEL,0,0));
                    app->fillCurveEditor();
                } break;
            case IDCANCEL:app->hide();break;
            } return 0;
        case trayMessage:
            if (LOWORD(lparam)==WM_CONTEXTMENU) app->menu();
            else if (LOWORD(lparam)==NIN_SELECT || LOWORD(lparam)==NIN_KEYSELECT) app->show();
            return 0;
        case stateMessage:app->render();return 0;
        case historyMessage:app->renderHistory();return 0;
        case exitMessage:DestroyWindow(window);return 0;
        case WM_DESTROY:KillTimer(window,1);if (app->rtss_) app->rtss_->release();PostQuitMessage(0);return 0;
        }
        return DefWindowProcW(window,message,wparam,lparam);
    }
};
} // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR arguments,int show) {
    const std::wstring argument=arguments ? arguments : L"";
    const bool startup=argument==L"--startup";
#ifdef FANCTL_PREVIEW_ONLY
    bool preview=true;
#else
    const bool preview=argument==L"--preview";
#endif
    if (!argument.empty() && !startup && !preview) return 2;
    if (!preview && !fanctl::isAdministrator()) {
        MessageBoxW(nullptr,L"Abra o FanController como administrador para acessar a interface Dell.",L"FanController",MB_OK|MB_ICONINFORMATION);
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
            MessageBoxW(nullptr,L"Não foi possível criar a janela.",L"FanController",MB_OK|MB_ICONERROR);result=1;
        } else {
            MSG message{};
            while (GetMessageW(&message,nullptr,0,0)>0) {
                if (!IsDialogMessageW(application.window(),&message)) {TranslateMessage(&message);DispatchMessageW(&message);}
            }
        }
    }
    ReleaseMutex(singleton);CloseHandle(singleton);
    return result;
}
