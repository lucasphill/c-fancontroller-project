#pragma once
// THROWAWAY PROTOTYPE: external OSD only. No RTSS, injection or fan control.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>
#pragma comment(lib,"gdiplus.lib")

namespace fanctl::prototype {
class OverlayWindow {
public:
    explicit OverlayWindow(HINSTANCE instance=GetModuleHandleW(nullptr)) : instance_(instance) {
        Gdiplus::GdiplusStartupInput startup;
        if(Gdiplus::GdiplusStartup(&gdiplus_,&startup,nullptr)!=Gdiplus::Ok) {
            status_=L"Falha ao iniciar desenho da faixa";return;
        }
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=windowProc;
        wc.hInstance=instance_;wc.lpszClassName=L"FanController.Prototype.ExternalStrip";
        if(!RegisterClassExW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) {
            status_=L"Falha ao registrar janela da faixa";return;
        }
        window_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
            wc.lpszClassName,L"FanController · OSD protótipo",WS_POPUP,0,0,0,0,nullptr,nullptr,instance_,nullptr);
        status_=window_?L"Aguardando Euro Truck em primeiro plano":L"Falha ao criar janela da faixa";
    }
    ~OverlayWindow() {
        if(window_)DestroyWindow(window_);
        if(gdiplus_)Gdiplus::GdiplusShutdown(gdiplus_);
    }
    OverlayWindow(const OverlayWindow&)=delete;
    OverlayWindow& operator=(const OverlayWindow&)=delete;
    HWND hwnd()const{return window_;}
    bool visible()const{return visible_;}
    bool truncated()const{return truncated_;}
    const std::wstring& status()const{return status_;}
    void hide() {
        if(window_ && visible_)ShowWindow(window_,SW_HIDE);
        visible_=false;
        status_=L"Faixa oculta";
    }
    bool update(HWND target,const std::wstring& text) {
        truncated_=false;
        if(!window_ || !gdiplus_)return false;
        if(!target || !IsWindow(target) || IsIconic(target) || !IsWindowVisible(target) ||
            GetAncestor(GetForegroundWindow(),GA_ROOT)!=GetAncestor(target,GA_ROOT) || !isEuroTruck(target)) {
            hide();status_=L"Oculto: Euro Truck não está em primeiro plano";return false;
        }
        RECT client{};POINT origin{};
        if(!GetClientRect(target,&client) || !ClientToScreen(target,&origin) || client.right<=0 || client.bottom<=0) {
            hide();status_=L"Área do jogo indisponível";return false;
        }
        if(text.empty()) {hide();status_=L"Nenhuma métrica selecionada";return false;}
        const UINT dpi=GetDpiForWindow(target);
        const float scale=static_cast<float>(dpi?dpi:96)/96.0f;
        const int margin=std::max(4,static_cast<int>(std::lround(8*scale)));
        const int available=std::min(16384,static_cast<int>(client.right)-2*margin);
        if(available<80) {hide();status_=L"Janela pequena demais para a faixa";return false;}
        HDC screen=GetDC(nullptr);
        if(!screen){hide();status_=L"Contexto de desenho indisponível";return false;}
        Gdiplus::FontFamily family(L"Segoe UI");
        Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
        format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap|Gdiplus::StringFormatFlagsMeasureTrailingSpaces);
        const int pad=std::max(4,static_cast<int>(std::lround(8*scale)));
        float fontSize=13*scale;
        std::wstring shown=text;
        Gdiplus::RectF measured{};
        {
            Gdiplus::Graphics measure(screen);
            for(int logical=13;logical>=10;--logical) {
                fontSize=static_cast<float>(logical)*scale;
                Gdiplus::Font font(&family,fontSize,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
                measure.MeasureString(shown.c_str(),static_cast<INT>(shown.size()),&font,Gdiplus::PointF(0,0),&format,&measured);
                if(measured.Width+2*pad<=available)break;
            }
            if(measured.Width+2*pad>available) {
                truncated_=true;
                Gdiplus::Font font(&family,fontSize,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
                while(!shown.empty()) {
                    shown.pop_back();
                    const auto candidate=shown+L"…";
                    measure.MeasureString(candidate.c_str(),static_cast<INT>(candidate.size()),&font,Gdiplus::PointF(0,0),&format,&measured);
                    if(measured.Width+2*pad<=available){shown=candidate;break;}
                }
            }
        }
        const int width=std::clamp(static_cast<int>(std::ceil(measured.Width))+2*pad,1,available);
        const int height=std::max(18,static_cast<int>(std::ceil(measured.Height))+std::max(6,static_cast<int>(6*scale)));
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* pixels=nullptr;
        HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        HDC memory=CreateCompatibleDC(screen);
        if(!bitmap || !memory) {
            if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);ReleaseDC(nullptr,screen);
            hide();status_=L"Falha ao alocar desenho da faixa";return false;
        }
        HGDIOBJ old=SelectObject(memory,bitmap);
        {
            Gdiplus::Bitmap image(width,height,width*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));
            Gdiplus::Graphics graphics(&image);
            graphics.Clear(Gdiplus::Color(0,0,0,0));
            graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
            graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            Gdiplus::SolidBrush background(Gdiplus::Color(178,12,17,24));
            graphics.FillRectangle(&background,0,0,width,height);
            Gdiplus::SolidBrush foreground(Gdiplus::Color(245,236,241,246));
            Gdiplus::Font font(&family,fontSize,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
            graphics.DrawString(shown.c_str(),static_cast<INT>(shown.size()),&font,
                Gdiplus::PointF(static_cast<float>(pad),static_cast<float>((height-measured.Height)/2)),&format,&foreground);
            graphics.Flush(Gdiplus::FlushIntentionSync);
        }
        POINT destination{origin.x+(client.right-width)/2,origin.y+margin};
        POINT source{};SIZE size{width,height};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        const bool success=UpdateLayeredWindow(window_,screen,&destination,&size,memory,&source,0,&blend,ULW_ALPHA)!=FALSE;
        SelectObject(memory,old);DeleteDC(memory);DeleteObject(bitmap);ReleaseDC(nullptr,screen);
        if(!success){hide();status_=L"Falha ao apresentar faixa externa";return false;}
        SetWindowPos(window_,HWND_TOPMOST,destination.x,destination.y,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
        visible_=true;
        status_=truncated_?L"Faixa truncada: amplie a janela ou selecione menos métricas":L"Faixa externa visível · sem captura de entrada";
        return true;
    }
private:
    static bool isEuroTruck(HWND target) {
        DWORD pid=0;GetWindowThreadProcessId(target,&pid);
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        if(!process)return false;
        wchar_t path[32768]{};DWORD length=32768;
        const bool queried=QueryFullProcessImageNameW(process,0,path,&length)!=FALSE;
        CloseHandle(process);
        if(!queried)return false;
        const wchar_t* basename=wcsrchr(path,L'\\');basename=basename?basename+1:path;
        return _wcsicmp(basename,L"eurotrucks2.exe")==0;
    }
    static LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
        if(message==WM_NCHITTEST)return HTTRANSPARENT;
        if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
        if(message==WM_ERASEBKGND)return TRUE;
        return DefWindowProcW(window,message,wparam,lparam);
    }
    HINSTANCE instance_=nullptr;
    HWND window_=nullptr;
    ULONG_PTR gdiplus_=0;
    bool visible_=false,truncated_=false;
    std::wstring status_;
};
} // namespace fanctl::prototype
