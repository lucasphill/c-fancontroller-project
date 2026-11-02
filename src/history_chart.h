#pragma once
#include "history_service.h"
#include <algorithm>
#include <cmath>
#include <ctime>

namespace fanctl {
inline std::wstring historyLabel(HistoryTime stamp) {
    const auto time=static_cast<__time64_t>(stamp);tm local{};
    if (_localtime64_s(&local,&time)) return L"—";
    wchar_t text[40]{};wcsftime(text,40,L"%d/%m %H:%M",&local);return text;
}
class HistoryChart {
public:
    explicit HistoryChart(int fan=0) : fan_(fan) {}
    HWND create(HWND parent,HINSTANCE instance,int fan) {
        fan_=fan;
        WNDCLASSW type{};type.lpfnWndProc=proc;type.hInstance=instance;
        type.lpszClassName=L"FanController.HistoryChart";type.hCursor=LoadCursorW(nullptr,IDC_CROSS);
        RegisterClassW(&type);
        window_=CreateWindowW(type.lpszClassName,L"",WS_CHILD,0,0,0,0,parent,nullptr,instance,this);
        return window_;
    }
    void update(const HistoryView& view) {view_=view;hover_=-1;if (window_) InvalidateRect(window_,nullptr,FALSE);}
    // The same renderer serves the window and deterministic visual QA images.
    void draw(HDC dc,const RECT& bounds,int dpi=96) {dpi_=dpi;paint(dc,bounds);}
private:
    HWND window_=nullptr;
    int fan_=0,hover_=-1;
    HistoryView view_;
    RECT plot_{};
    int dpi_=96;
    int scale(int value) const {return MulDiv(value,dpi_,96);}
    static COLORREF color(int metric) {
        const COLORREF colors[]{RGB(210,65,45),RGB(128,70,185),RGB(0,121,151)};return colors[metric];
    }
    static std::optional<int> metric(const HistoryFan& fan,int index) {
        return index==0 ? fan.temperature : index==1 ? fan.boost : fan.rpm;
    }
    void text(HDC dc,int x,int y,const std::wstring& value,COLORREF ink=RGB(30,41,59)) const {
        SetTextColor(dc,ink);TextOutW(dc,scale(x),scale(y),value.c_str(),static_cast<int>(value.size()));
    }
    int x(HistoryTime time) const {
        return plot_.left+static_cast<int>((time-view_.from)*static_cast<double>(plot_.right-plot_.left)/
            static_cast<double>(std::max<HistoryTime>(1,view_.until-view_.from)));
    }
    void paint(HDC dc,const RECT& bounds) {
        const int width=MulDiv(bounds.right,96,dpi_),height=MulDiv(bounds.bottom,96,dpi_);
        HBRUSH background=CreateSolidBrush(RGB(255,255,255));FillRect(dc,&bounds,background);DeleteObject(background);
        SetBkMode(dc,TRANSPARENT);
        HFONT font=CreateFontW(-scale(11),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        const auto oldFont=SelectObject(dc,font);
        text(dc,10,5,fan_==0 ? L"CPU provável · fan 50 · Dell" : L"GPU · fan 51 · troca Dell/NVML: tracejado");
        text(dc,10,23,L"Temperatura °C",color(0));text(dc,172,23,L"Boost lido",color(1));text(dc,340,23,L"Rotação RPM",color(2));
        plot_={scale(44),scale(62),scale(width-94),scale(height-47)};
        int boostMax=100,rpmMax=6000;
        for (const auto& sample : *view_.samples) {
            const auto& fan=sample.fans[fan_];
            if (fan.boost) boostMax=std::max(boostMax,((*fan.boost+49)/50)*50);
            if (fan.rpm) rpmMax=std::max(rpmMax,((*fan.rpm+999)/1000)*1000);
        }
        const int maximum[]{125,boostMax,rpmMax};
        text(dc,4,43,L"°C",color(0));text(dc,width-87,43,L"boost",color(1));text(dc,width-42,43,L"RPM",color(2));
        HPEN grid=CreatePen(PS_SOLID,1,RGB(225,231,237));const auto oldPen=SelectObject(dc,grid);
        for (int tick=0;tick<=5;++tick) {
            const int y=plot_.bottom-(plot_.bottom-plot_.top)*tick/5;
            MoveToEx(dc,plot_.left,y,nullptr);LineTo(dc,plot_.right,y);
            const int logicalY=MulDiv(y,96,dpi_)-7;
            text(dc,8,logicalY,std::to_wstring(25*tick),color(0));
            text(dc,width-86,logicalY,std::to_wstring(boostMax*tick/5),color(1));
            text(dc,width-43,logicalY,std::to_wstring(rpmMax*tick/5),color(2));
        }
        SelectObject(dc,oldPen);DeleteObject(grid);
        text(dc,44,height-44,historyLabel(view_.from));
        text(dc,width-189,height-44,historyLabel(view_.until));
        for (int series=0;series<3;++series) {
            HPEN pen=CreatePen(PS_SOLID,scale(2),color(series));const auto before=SelectObject(dc,pen);
            HPEN transitionPen=fan_==1 && series==0 ? CreatePen(PS_DASH,1,color(series)) : nullptr;
            bool previous=false;HistoryTime previousTime=0;GpuSource previousSource=GpuSource::Dell;
            // Preserve spikes and null/source boundaries when several samples share a pixel.
            int lastX=-1,minY=0,maxY=0,lastY=0;
            auto flush=[&] {if (lastX>=0) {MoveToEx(dc,lastX,minY,nullptr);LineTo(dc,lastX,maxY+1);}};
            for (const auto& sample : *view_.samples) {
                const auto value=metric(sample.fans[fan_],series);
                if (!value || sample.time<view_.from || sample.time>view_.until) {flush();previous=false;lastX=-1;continue;}
                const int px=x(sample.time);
                const int py=plot_.bottom-static_cast<int>(std::clamp(*value,0,maximum[series])*
                    static_cast<double>(plot_.bottom-plot_.top)/maximum[series]);
                const bool connect=previous && sample.time-previousTime<=450;
                const bool changedSource=fan_==1 && series==0 && previousSource!=sample.gpuSource;
                if (!connect || lastX!=px) {
                    flush();
                    if (connect && lastX>=0) {
                        // Show the provenance of adjacent valid temperatures instead
                        // of hiding the curve whenever Dell and NVML alternate.
                        const bool transition=changedSource && transitionPen;
                        if (transition) SelectObject(dc,transitionPen);
                        MoveToEx(dc,lastX,lastY,nullptr);LineTo(dc,px,py);
                        if (transition) SelectObject(dc,pen);
                    }
                    else {MoveToEx(dc,px,py,nullptr);LineTo(dc,px+1,py);}
                    lastX=px;minY=maxY=py;
                } else {minY=std::min(minY,py);maxY=std::max(maxY,py);}
                lastY=py;previous=true;previousTime=sample.time;previousSource=sample.gpuSource;
            }
            flush();SelectObject(dc,before);DeleteObject(pen);if (transitionPen) DeleteObject(transitionPen);
        }
        if (view_.samples->empty()) text(dc,85,100,view_.loading ? L"Carregando histórico…" : L"Sem amostras neste período.");
        if (hover_>=0 && static_cast<std::size_t>(hover_)<view_.samples->size()) {
            const auto& sample=(*view_.samples)[static_cast<std::size_t>(hover_)];const auto& fan=sample.fans[fan_];
            auto value=[](std::optional<int> number) {return number ? std::to_wstring(*number) : std::wstring(L"—");};
            std::wstring detail=historyLabel(sample.time)+L"  |  "+value(fan.temperature)+L" °C  ·  boost "+value(fan.boost)+L"  ·  "+value(fan.rpm)+L" RPM";
            if (fan_==1) detail+=sample.gpuSource==GpuSource::Nvml ? L" · NVML" : L" · Dell (não validada)";
            text(dc,10,height-22,detail);
            HPEN cursor=CreatePen(PS_DOT,1,RGB(80,90,100));const auto before=SelectObject(dc,cursor);
            const int px=x(sample.time);MoveToEx(dc,px,plot_.top,nullptr);LineTo(dc,px,plot_.bottom);
            SelectObject(dc,before);DeleteObject(cursor);
        } else text(dc,10,height-22,L"Passe o mouse para consultar uma amostra. Horários locais.");
        SelectObject(dc,oldFont);DeleteObject(font);
    }
    void mouse(int px,int py) {
        int selected=-1;
        if (px>=plot_.left && px<=plot_.right && py>=plot_.top && py<=plot_.bottom && !view_.samples->empty()) {
            const auto time=view_.from+static_cast<HistoryTime>((px-plot_.left)*static_cast<double>(view_.until-view_.from)/
                std::max<LONG>(1,plot_.right-plot_.left));
            const auto& samples=*view_.samples;
            auto found=std::lower_bound(samples.begin(),samples.end(),time,[](const HistorySample& sample,HistoryTime target) {return sample.time<target;});
            if (found==samples.end()) --found;
            else if (found!=samples.begin() && time-(found-1)->time<found->time-time) --found;
            if (std::abs(x(found->time)-px)<=scale(12)) selected=static_cast<int>(found-samples.begin());
        }
        if (selected!=hover_) {hover_=selected;InvalidateRect(window_,nullptr,FALSE);}
        TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window_,0};TrackMouseEvent(&track);
    }
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
        auto* chart=reinterpret_cast<HistoryChart*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if (message==WM_NCCREATE) {
            chart=static_cast<HistoryChart*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            chart->window_=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(chart));
        }
        if (!chart) return DefWindowProcW(window,message,wparam,lparam);
        if (message==WM_ERASEBKGND) return 1;
        if (message==WM_MOUSEMOVE) {chart->mouse(static_cast<short>(LOWORD(lparam)),static_cast<short>(HIWORD(lparam)));return 0;}
        if (message==WM_MOUSELEAVE) {chart->hover_=-1;InvalidateRect(window,nullptr,FALSE);return 0;}
        if (message==WM_PAINT) {
            PAINTSTRUCT paint{};const auto dc=BeginPaint(window,&paint);RECT bounds{};GetClientRect(window,&bounds);
            const auto memory=CreateCompatibleDC(dc);const auto bitmap=CreateCompatibleBitmap(dc,bounds.right,bounds.bottom);
            const auto old=SelectObject(memory,bitmap);chart->draw(memory,bounds,static_cast<int>(GetDpiForWindow(window)));
            BitBlt(dc,0,0,bounds.right,bounds.bottom,memory,0,0,SRCCOPY);
            SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);EndPaint(window,&paint);return 0;
        }
        return DefWindowProcW(window,message,wparam,lparam);
    }
};
} // namespace fanctl
