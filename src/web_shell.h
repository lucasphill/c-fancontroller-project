#pragma once
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <shlobj.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace fanctl {
class WebShell {
public:
    std::function<void(const std::wstring&)> command;
    std::function<void(const std::wstring&)> failed;
    ~WebShell(){*alive_=false;if(controller_)controller_->Close();}
    void create(HWND parent,bool preview){
        parent_=parent;PWSTR local=nullptr;
        if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_CREATE,nullptr,&local))){error(L"Pasta de dados indisponível.");return;}
        auto folder=std::filesystem::path(local)/(preview?L"aeri-preview":L"FanController")/L"WebView";CoTaskMemFree(local);
        auto alive=alive_;
        const auto hr=CreateCoreWebView2EnvironmentWithOptions(nullptr,folder.c_str(),nullptr,
            Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([this,alive](HRESULT result,ICoreWebView2Environment* environment)->HRESULT{
                if(!*alive)return S_OK;if(FAILED(result)||!environment){error(L"O runtime Microsoft Edge WebView2 não está disponível. Instale o runtime Evergreen para abrir a aeri.");return S_OK;}
                environment->CreateCoreWebView2Controller(parent_,Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([this,alive](HRESULT result2,ICoreWebView2Controller* controller)->HRESULT{
                    if(!*alive)return S_OK;if(FAILED(result2)||!controller){error(L"Não foi possível criar a interface aeri.");return S_OK;}
                    controller_=controller;controller_->get_CoreWebView2(&view_);Microsoft::WRL::ComPtr<ICoreWebView2Controller2> translucent;
                    if(SUCCEEDED(controller_.As(&translucent)))translucent->put_DefaultBackgroundColor({0,0,0,0});
                    Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;view_->get_Settings(&settings);settings->put_IsStatusBarEnabled(FALSE);settings->put_AreDefaultContextMenusEnabled(FALSE);settings->put_AreDevToolsEnabled(FALSE);settings->put_IsZoomControlEnabled(FALSE);settings->put_AreHostObjectsAllowed(FALSE);
                    EventRegistrationToken token{};
                    view_->add_WebMessageReceived(Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>([this,alive](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* args)->HRESULT{
                        if(!*alive)return S_OK;LPWSTR source=nullptr;args->get_Source(&source);const bool trusted=source&&std::wstring(source)==L"about:blank";CoTaskMemFree(source);if(!trusted)return S_OK;
                        LPWSTR value=nullptr;if(SUCCEEDED(args->TryGetWebMessageAsString(&value))){std::wstring copy=value?value:L"";CoTaskMemFree(value);if(copy.size()<8192&&command)command(copy);}return S_OK;
                    }).Get(),&token);
                    view_->add_NavigationStarting(Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>([this](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT{LPWSTR uri=nullptr;args->get_Uri(&uri);const std::wstring target=uri?uri:L"";const bool allowed=target==L"about:blank"||(bootstrap_&&target.rfind(L"data:text/html",0)==0);bootstrap_=false;CoTaskMemFree(uri);if(!allowed)args->put_Cancel(TRUE);return S_OK;}).Get(),&token);
                    view_->add_NewWindowRequested(Microsoft::WRL::Callback<ICoreWebView2NewWindowRequestedEventHandler>([](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* args)->HRESULT{args->put_Handled(TRUE);return S_OK;}).Get(),&token);
                    view_->add_NavigationCompleted(Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>([this,alive](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs* args)->HRESULT{if(!*alive)return S_OK;BOOL success=FALSE;args->get_IsSuccess(&success);if(!success)error(L"Não foi possível carregar os recursos locais da interface.");else controller_->put_IsVisible(TRUE);return S_OK;}).Get(),&token);
                    const auto resource=FindResourceW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(400),RT_RCDATA);const auto loaded=LoadResource(GetModuleHandleW(nullptr),resource);const auto bytes=static_cast<const char*>(LockResource(loaded));const int length=static_cast<int>(SizeofResource(GetModuleHandleW(nullptr),resource));
                    if(!bytes||!length){error(L"Recursos da interface ausentes.");return S_OK;}int n=MultiByteToWideChar(CP_UTF8,0,bytes,length,nullptr,0);std::wstring page(static_cast<std::size_t>(n),0);MultiByteToWideChar(CP_UTF8,0,bytes,length,page.data(),n);resize();view_->NavigateToString(page.c_str());return S_OK;
                }).Get());return S_OK;
            }).Get());if(FAILED(hr))error(L"Não foi possível iniciar o WebView2.");
    }
    void resize(){if(controller_){RECT r{};GetClientRect(parent_,&r);controller_->put_Bounds(r);}}
    void visible(bool visible){
        if(!controller_)return;
        Microsoft::WRL::ComPtr<ICoreWebView2_3> lifecycle;
        if(view_)view_.As(&lifecycle);
        if(visible){if(lifecycle)lifecycle->Resume();controller_->put_IsVisible(TRUE);}
        else{controller_->put_IsVisible(FALSE);if(lifecycle)lifecycle->TrySuspend(Microsoft::WRL::Callback<ICoreWebView2TrySuspendCompletedHandler>([](HRESULT,BOOL)->HRESULT{return S_OK;}).Get());}
    }
    void post(const std::wstring& json){if(view_)view_->PostWebMessageAsJson(json.c_str());}
private:
    HWND parent_=nullptr;bool bootstrap_=true;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> view_;
    std::shared_ptr<bool> alive_=std::make_shared<bool>(true);
    void error(const std::wstring& message){if(failed)failed(message);}
};
inline std::wstring jsonString(const std::wstring& text){std::wstring out=L"\"";for(wchar_t c:text){switch(c){case L'"':out+=L"\\\"";break;case L'\\':out+=L"\\\\";break;case L'\n':out+=L"\\n";break;case L'\r':out+=L"\\r";break;case L'\t':out+=L"\\t";break;default:if(c>=32)out+=c;break;}}return out+L'"';}
} // namespace fanctl
