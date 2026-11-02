#pragma once
// External, input-transparent OSD. All window operations belong to its creator UI thread.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>
#include <map>
#include <vector>
#pragma comment(lib,"gdiplus.lib")

namespace fanctl {
inline DWORD euroTruckProcessId(HWND target) {
    if(!target || !IsWindow(target))return 0;
    DWORD pid=0;GetWindowThreadProcessId(target,&pid);
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process)return 0;
    wchar_t path[32768]{};DWORD length=32768;
    const bool queried=QueryFullProcessImageNameW(process,0,path,&length)!=FALSE;
    CloseHandle(process);
    if(!queried)return 0;
    const wchar_t* name=wcsrchr(path,L'\\');name=name?name+1:path;
    return _wcsicmp(name,L"eurotrucks2.exe")==0?pid:0;
}
inline HWND findEuroTruckWindow() {
    HWND result=nullptr;
    EnumWindows([](HWND window,LPARAM output)->BOOL {
        if(IsWindowVisible(window) && GetWindow(window,GW_OWNER)==nullptr && euroTruckProcessId(window)) {
            *reinterpret_cast<HWND*>(output)=window;return FALSE;
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&result));return result;
}
class OverlayWindow {
public:
    explicit OverlayWindow(HINSTANCE instance=GetModuleHandleW(nullptr)) : instance_(instance) {
        Gdiplus::GdiplusStartupInput startup;
        if(Gdiplus::GdiplusStartup(&gdiplus_,&startup,nullptr)!=Gdiplus::Ok) {
            status_=L"Falha ao iniciar desenho da faixa";return;
        }
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=windowProc;
        wc.hInstance=instance_;wc.lpszClassName=L"FanController.ExternalStrip";
        if(!RegisterClassExW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) {
            status_=L"Falha ao registrar janela da faixa";return;
        }
        window_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
            wc.lpszClassName,L"FanController · OSD",WS_POPUP,0,0,0,0,nullptr,nullptr,instance_,this);
        if(window_) {
            addHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND);
            addHook(EVENT_SYSTEM_MINIMIZESTART,EVENT_SYSTEM_MINIMIZEEND);
            addHook(EVENT_OBJECT_LOCATIONCHANGE,EVENT_OBJECT_LOCATIONCHANGE);
            // Events are delivered outside the game on this UI thread. A timer also
            // validates lifetime and provides a bounded fallback if a hook is denied.
            SetTimer(window_,1,hooks_.size()==3?500:100,nullptr);
        }
        status_=window_?L"Aguardando Euro Truck em primeiro plano":L"Falha ao criar janela da faixa";
    }
    ~OverlayWindow() {
        for(const auto hook:hooks_){eventOwners_.erase(hook);UnhookWinEvent(hook);}
        if(window_)KillTimer(window_,1);
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
        target_=nullptr;text_.clear();
        hideForFocus(L"Faixa oculta");
    }
    bool update(HWND target,const std::wstring& text) {
        target_=target;text_=text;
        return refresh();
    }
private:
    void hideForFocus(const wchar_t* reason) {
        if(window_ && visible_)ShowWindow(window_,SW_HIDE);
        visible_=false;
        status_=reason;
    }
    bool refresh() {
        if(refreshing_)return visible_;
        struct Reset {bool& value;~Reset(){value=false;}} reset{refreshing_};refreshing_=true;
        const auto target=target_;
        const auto& text=text_;
        truncated_=false;
        if(!window_ || !gdiplus_)return false;
        if(!target || !IsWindow(target) || IsIconic(target) || !IsWindowVisible(target) ||
            GetAncestor(GetForegroundWindow(),GA_ROOT)!=GetAncestor(target,GA_ROOT) || !euroTruckProcessId(target)) {
            hideForFocus(L"Oculto: Euro Truck não está em primeiro plano");return false;
        }
        RECT client{};POINT origin{};
        if(!GetClientRect(target,&client) || !ClientToScreen(target,&origin) || client.right<=0 || client.bottom<=0) {
            hideForFocus(L"Área do jogo indisponível");return false;
        }
        if(text.empty()) {hideForFocus(L"Nenhuma métrica selecionada");return false;}
        const UINT dpi=GetDpiForWindow(target);
        const float scale=static_cast<float>(dpi?dpi:96)/96.0f;
        const int margin=std::max(4,static_cast<int>(std::lround(8*scale)));
        const int available=std::min(16384,static_cast<int>(client.right)-2*margin);
        if(available<80) {hideForFocus(L"Janela pequena demais para a faixa");return false;}
        if(visible_ && text==paintedText_ && origin.x==lastOrigin_.x && origin.y==lastOrigin_.y &&
            client.right==lastClient_.right && client.bottom==lastClient_.bottom && dpi==lastDpi_) {
            truncated_=lastTruncated_;return true;
        }
        HDC screen=GetDC(nullptr);
        if(!screen){hideForFocus(L"Contexto de desenho indisponível");return false;}
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
            hideForFocus(L"Falha ao alocar desenho da faixa");return false;
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
        if(!success){hideForFocus(L"Falha ao apresentar faixa externa");return false;}
        if(!SetWindowPos(window_,HWND_TOPMOST,destination.x,destination.y,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW)) {
            hideForFocus(L"Falha ao posicionar faixa externa");return false;
        }
        visible_=true;
        paintedText_=text;lastOrigin_=origin;lastClient_=client;lastDpi_=dpi;lastTruncated_=truncated_;
        status_=truncated_?L"Faixa truncada: amplie a janela ou selecione menos métricas":L"Faixa externa visível · sem captura de entrada";
        return true;
    }
    void addHook(DWORD first,DWORD last) {
        const auto hook=SetWinEventHook(first,last,nullptr,eventProc,0,0,WINEVENT_OUTOFCONTEXT);
        if(hook){eventOwners_[hook]=this;hooks_.push_back(hook);}
    }
    void refreshNoThrow() noexcept {
        try{refresh();}
        catch(...) {
            // A window callback must never unwind through Windows or the fan UI.
            if(window_)ShowWindow(window_,SW_HIDE);
            visible_=false;
            try{status_=L"Faixa indisponível: falha de apresentação";}catch(...){}
        }
    }
    static void CALLBACK eventProc(HWINEVENTHOOK hook,DWORD event,HWND target,LONG object,LONG,DWORD,DWORD) {
        const auto owner=eventOwners_.find(hook);if(owner==eventOwners_.end())return;
        auto* self=owner->second;
        if(!self->target_)return;
        if(event==EVENT_SYSTEM_MINIMIZESTART && target==self->target_) {
            self->hideForFocus(L"Oculto: jogo minimizado");return;
        }
        if(event==EVENT_SYSTEM_FOREGROUND ||
            (target==self->target_ && (event==EVENT_SYSTEM_MINIMIZEEND || object==OBJID_WINDOW)))self->refreshNoThrow();
    }
    static LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
        auto* self=reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE) {
            self=static_cast<OverlayWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(message==WM_TIMER && wparam==1 && self){self->refreshNoThrow();return 0;}
        if(message==WM_NCHITTEST)return HTTRANSPARENT;
        if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
        if(message==WM_ERASEBKGND)return TRUE;
        return DefWindowProcW(window,message,wparam,lparam);
    }
    HINSTANCE instance_=nullptr;
    HWND window_=nullptr;
    ULONG_PTR gdiplus_=0;
    bool visible_=false,truncated_=false;
    bool refreshing_=false,lastTruncated_=false;
    HWND target_=nullptr;
    POINT lastOrigin_{};RECT lastClient_{};UINT lastDpi_=0;
    std::wstring text_,paintedText_;
    std::vector<HWINEVENTHOOK> hooks_;
    inline static thread_local std::map<HWINEVENTHOOK,OverlayWindow*> eventOwners_;
    std::wstring status_;
};
} // namespace fanctl
