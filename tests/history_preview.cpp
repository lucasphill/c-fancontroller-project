// Visual QA harness: synthetic telemetry only, no hardware or user database access.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>
#include "../src/history_chart.h"

namespace {
fanctl::HistoryChart charts[2]{fanctl::HistoryChart(0),fanctl::HistoryChart(1)};
HWND period=nullptr;
void refresh(int index=-1) {
    const fanctl::HistoryTime periods[]{3600,86400,7*86400,30*86400,90*86400};
    const auto selected=index<0 ? SendMessageW(period,CB_GETCURSEL,0,0) : index;
    fanctl::HistoryView view;view.loading=false;view.until=fanctl::historyNow();view.from=view.until-periods[selected];
    auto samples=std::make_shared<std::vector<fanctl::HistorySample>>();
    for (auto time=view.from;time<=view.until;time+=300) {
        const auto i=static_cast<int>((time-view.from)/300);
        fanctl::HistorySample sample;sample.time=time;
        sample.gpuSource=i%100<50 ? fanctl::GpuSource::Dell : fanctl::GpuSource::Nvml;
        for (int fan=0;fan<2;++fan) {
            sample.fans[fan].temperature=55+static_cast<int>(15*std::sin(i*0.07+fan));
            sample.fans[fan].boost=45+static_cast<int>(15*std::sin(i*0.06+fan));
            sample.fans[fan].rpm=2700+static_cast<int>(1300*std::sin(i*0.05+fan));
        }
        if (i%100>=75 && i%100<=85) sample.fans[1].temperature.reset();
        if (i%100>=20 && i%100<=25) continue;
        samples->push_back(sample);
    }
    view.samples=samples;
    for (auto& chart : charts) chart.update(view);
}
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    if (message==WM_COMMAND && HIWORD(wparam)==CBN_SELCHANGE) {refresh();return 0;}
    if (message==WM_DESTROY) {PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,wparam,lparam);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR arguments,int) {
    const bool gpuRegression=std::wstring(arguments)==L"--render-gpu-regression";
    if (std::wstring(arguments)==L"--render" || gpuRegression) {
        Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;
        if (Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok) return 1;
        const CLSID pngEncoder{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
        int result=0;
        const std::vector<int> periods=gpuRegression ? std::vector<int>{0} : std::vector<int>{1,4};
        for (int index : periods) {
            if (gpuRegression) {
                // Replay the seven recorded GPU readings from the reported period.
                fanctl::HistoryView view;view.loading=false;view.from=1792940955;view.until=1792944597;
                auto samples=std::make_shared<std::vector<fanctl::HistorySample>>();
                const int rows[][5]{{1792942755,30,0,0,0},{1792943057,46,39,3355,1},
                    {1792943482,60,0,2039,0},{1792943783,45,41,3432,1},
                    {1792943936,30,0,0,0},{1792944236,45,33,3213,1},{1792944537,45,33,3203,1}};
                for (const auto& row : rows) {
                    fanctl::HistorySample sample;sample.time=row[0];sample.fans[1]={row[1],row[2],row[3]};
                    sample.gpuSource=row[4] ? fanctl::GpuSource::Nvml : fanctl::GpuSource::Dell;samples->push_back(sample);
                }
                view.samples=samples;charts[1].update(view);
            } else refresh(index);
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=592;
            info.bmiHeader.biHeight=gpuRegression ? -212 : -436;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
            void* bits=nullptr;const auto bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);
            const auto dc=CreateCompatibleDC(nullptr);const auto old=SelectObject(dc,bitmap);
            RECT bounds{0,0,592,212};
            if (gpuRegression) charts[1].draw(dc,bounds);
            else {charts[0].draw(dc,bounds);SetViewportOrgEx(dc,0,224,nullptr);charts[1].draw(dc,bounds);}
            GdiFlush();
            {
                Gdiplus::Bitmap image(bitmap,nullptr);
                const auto name=gpuRegression ? L"gpu-temperature-regression.png" :
                    index==1 ? L"history-preview-24h.png" : L"history-preview-90d.png";
                if (image.Save(name,&pngEncoder,nullptr)!=Gdiplus::Ok) result=1;
            }
            SelectObject(dc,old);DeleteDC(dc);DeleteObject(bitmap);
        }
        Gdiplus::GdiplusShutdown(token);return result;
    }
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSW type{};type.lpfnWndProc=procedure;type.hInstance=instance;type.lpszClassName=L"FanController.HistoryPreview";
    type.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);RegisterClassW(&type);
    RECT bounds{0,0,640,740};AdjustWindowRect(&bounds,WS_OVERLAPPEDWINDOW,FALSE);
    const auto window=CreateWindowW(type.lpszClassName,L"FanController — prévia do histórico (dados simulados)",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,nullptr);
    const auto label=CreateWindowW(L"STATIC",L"Histórico — dados simulados para validação visual",WS_CHILD|WS_VISIBLE,
        24,30,592,30,window,nullptr,instance,nullptr);
    period=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,160,130,456,160,window,nullptr,instance,nullptr);
    for (const auto* name : {L"Última hora",L"Últimas 24 horas",L"Últimos 7 dias",L"Últimos 30 dias",L"Últimos 90 dias"})
        SendMessageW(period,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
    SendMessageW(period,CB_SETCURSEL,1,0);
    const auto font=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessageW(label,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);SendMessageW(period,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    for (int fan=0;fan<2;++fan) {
        const auto chart=charts[fan].create(window,instance,fan);
        MoveWindow(chart,24,206+224*fan,592,212,TRUE);ShowWindow(chart,SW_SHOW);
    }
    refresh();ShowWindow(window,SW_SHOW);
    MSG message{};while (GetMessageW(&message,nullptr,0,0)>0) {TranslateMessage(&message);DispatchMessageW(&message);}
    return 0;
}
