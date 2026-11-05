// Estúdio's approved layout is rendered locally; hardware stays on the C++ worker.
    void backdrop() {
        BOOL dark=TRUE;DwmSetWindowAttribute(window_,20,&dark,sizeof(dark));
        const int material=glass_?3:1;glassAvailable_=SUCCEEDED(DwmSetWindowAttribute(window_,38,&material,sizeof(material)))&&glass_;
        MARGINS margins=glassAvailable_?MARGINS{-1,-1,-1,-1}:MARGINS{};DwmExtendFrameIntoClientArea(window_,&margins);InvalidateRect(window_,nullptr,TRUE);
    }
    void createAeri() {
        if(!preview_)try{const auto saved=fanctl::readPersistent(L"aeri-appearance.txt");glass_=!saved||*saved!="opaque";}catch(const std::exception&){}
        web_.command=[this](const std::wstring& command){webCommand(command);};
        web_.failed=[this](const std::wstring& error){MessageBoxW(window_,error.c_str(),L"aeri",MB_OK|MB_ICONERROR);};
        backdrop();if(!startupLaunch_||!trayAdded_||!settingsError_.empty()){webCreated_=true;web_.create(window_,preview_);}
    }
    static std::wstring jsonNumber(std::optional<double> n){return n&&std::isfinite(*n)?std::to_wstring(*n):L"null";}
    static std::wstring jsonInteger(std::optional<int> n){return n?std::to_wstring(*n):L"null";}
    static std::wstring jsonCurves(const fanctl::CurveSettings& settings){std::wstring out=L"[";for(int p=0;p<2;++p){if(p)out+=L",";out+=L"[";for(int f=0;f<2;++f){if(f)out+=L",";out+=L"[";bool first=true;for(const auto& point:settings[p][f]){if(!first)out+=L",";first=false;out+=L"{\"temperature\":"+std::to_wstring(point.temperature)+L",\"boost\":"+std::to_wstring(point.boost)+L"}";}out+=L"]";}out+=L"]";}return out+L"]";}
    void publishWebState(){if(!webReady_||!window_)return;const auto view=current();
        std::wstring out=L"{\"type\":\"state\",\"now\":"+std::to_wstring(fanctl::historyNow())+L",\"preview\":"+(preview_?L"true":L"false")+L",\"ready\":"+(view.ready||preview_?L"true":L"false")+L",\"busy\":"+(view.busy?L"true":L"false")+L",\"active\":"+(view.active?L"true":L"false")+L",\"pending\":"+(view.pending?L"true":L"false")+L",\"mode\":"+std::to_wstring(view.mode)+L",\"glass\":"+(glass_?L"true":L"false")+L",\"error\":"+fanctl::jsonString(!view.error.empty()?view.error:settingsError_)+L",\"configuration\":"+fanctl::jsonString(preview_?L"Prévia · nenhum controle de hardware":wide(view.configuration))+L",\"curves\":"+jsonCurves(curves_)+L",\"osd\":{\"enabled\":"+(osdSettings_.enabled?L"true":L"false")+L",\"metrics\":"+std::to_wstring(osdSettings_.metrics)+L",\"status\":"+fanctl::jsonString(osdStatus_)+L"},\"live\":[";
        for(int i=0;i<2;++i){if(i)out+=L",";out+=L"{\"temperature\":"+jsonNumber(i?liveValues_.gpuTemperature:liveValues_.cpuTemperature)+L",\"rpm\":"+jsonNumber(i?liveValues_.gpuRpm:liveValues_.cpuRpm)+L",\"boost\":";std::optional<int> boost;if(view.frame)for(const auto& fan:view.frame->snapshot.fans)if(fan.id==static_cast<std::uint32_t>(50+i))boost=static_cast<int>(fan.boost);out+=jsonInteger(boost)+L"}";}web_.post(out+L"]}");
    }
    void publishWebHistory(const fanctl::HistoryView& history){if(!webReady_)return;
        std::wstring out=L"{\"type\":\"history\",\"from\":"+std::to_wstring(history.from)+L",\"until\":"+std::to_wstring(history.until)+L",\"error\":"+fanctl::jsonString(wide(history.error))+L",\"samples\":[";bool first=true;
        const auto rows=fanctl::historyForDisplay(*history.samples,history.from,history.until);
        for(const auto& row:rows){if(!first)out+=L",";first=false;out+=L"{\"time\":"+std::to_wstring(row.time)+L",\"end\":"+std::to_wstring(row.end)+L",\"interval\":"+std::to_wstring(row.interval)+L",\"count\":"+std::to_wstring(row.count)+L",\"segment\":"+fanctl::jsonString(wide(row.segment))+L",\"source\":"+(row.gpuSource==fanctl::GpuSource::Nvml?L"1":L"0")+L",\"configuration\":"+fanctl::jsonString(wide(row.configuration))+L",\"fans\":[";
            for(int f=0;f<2;++f){if(f)out+=L",";const auto& v=row.fans[f];out+=L"{\"temperature\":"+jsonInteger(v.temperature)+L",\"rpm\":"+jsonInteger(v.rpm)+L",\"boost\":"+jsonInteger(v.boost)+L",\"temperatureMin\":"+jsonInteger(v.temperatureMin)+L",\"temperatureMax\":"+jsonInteger(v.temperatureMax)+L",\"rpmMin\":"+jsonInteger(v.rpmMin)+L",\"rpmMax\":"+jsonInteger(v.rpmMax)+L"}";}out+=L"]}";
        }out+=L"],\"events\":[";first=true;for(const auto& event:history.events){if(!first)out+=L",";first=false;out+=L"{\"time\":"+std::to_wstring(event.time)+L",\"label\":"+fanctl::jsonString(wide(event.label))+L",\"configuration\":"+fanctl::jsonString(wide(event.configuration))+L"}";}web_.post(out+L"]}");
    }
    void webCommand(const std::wstring& text){std::wistringstream in(text);std::wstring action;in>>action;
        try{
            if(action==L"ready"){webReady_=true;publishWebState();if(preview_){webHistoryVisible_=true;renderHistory();webHistoryVisible_=false;}return;}
            if(action==L"view"){std::wstring page;in>>page;webHistoryVisible_=page==L"history";if(!webHistoryVisible_&&history_)history_->period(900);return;}
            if(action==L"history"){webHistoryVisible_=true;long long seconds=0;if(!(in>>seconds)||(seconds!=900&&seconds!=3600&&seconds!=86400&&seconds!=604800&&seconds!=7776000))throw std::runtime_error("Periodo invalido.");if(history_)history_->period(seconds);else renderHistory();return;}
            if(action==L"range"){long long from=0,until=0;if(!(in>>from>>until)||until<=from||from<fanctl::historyNow()-fanctl::historyRetention||until>fanctl::historyNow()+60)throw std::runtime_error("Trecho invalido.");if(history_)history_->range(from,until);return;}
            if(action==L"exit"){if(preview_)DestroyWindow(window_);else send({Action::Exit});return;}
            if(action==L"restore"){send({Action::Restore});return;}
            if(action==L"apply"){int mode=0,a=0,b=0;if(!(in>>mode>>a>>b)||mode<0||mode>2||a<0||a>100||b<0||b>100)throw std::runtime_error("Ajustes invalidos.");const auto view=current();if(!preview_&&(!view.ready||view.busy||view.pending))throw std::runtime_error("Aguarde a conclusao do controle atual.");if(mode&&!settingsError_.empty())throw std::runtime_error("Revise e aplique as curvas antes de ativar um perfil automatico.");Request request{Action::Apply,a,b};request.mode=mode;request.curves=curves_;if(preview_){std::lock_guard<std::mutex> lock(mutex_);view_.active=true;view_.mode=mode;}else send(request);publishWebState();return;}
            if(action==L"curves"){fanctl::CurveSettings next;for(auto& p:next)for(auto& curve:p){int count=0;if(!(in>>count)||count<2||count>16)throw std::runtime_error("Use de 2 a 16 pontos por curva.");curve.resize(static_cast<std::size_t>(count));for(auto& point:curve)if(!(in>>point.temperature>>point.boost))throw std::runtime_error("Curvas incompletas.");}std::wstring extra;if(in>>extra)throw std::runtime_error("Dados extras nas curvas.");fanctl::validateCurves(next);if(!preview_)saveCurves(next);curves_=next;settingsError_.clear();if(!preview_){Request request{Action::Curves};request.curves=next;send(request);}publishWebState();web_.post(L"{\"type\":\"ack\",\"command\":\"curves\",\"ok\":true}");return;}
            if(action==L"glass"){int enabled=0;if(!(in>>enabled)||(enabled!=0&&enabled!=1))throw std::runtime_error("Aparencia invalida.");glass_=enabled!=0;if(!preview_)fanctl::writePersistent(L"aeri-appearance.txt",glass_?"glass":"opaque");backdrop();return;}
            if(action==L"osd"){int enabled=0,metrics=0;if(!(in>>enabled>>metrics)||(enabled!=0&&enabled!=1)||metrics<0||metrics>255)throw std::runtime_error("Metricas invalidas.");fanctl::CompactOsdSettings settings;settings.enabled=enabled!=0;settings.metrics=static_cast<unsigned>(metrics);if(!preview_)fanctl::saveCompactOsdSettings(settings);osdSettings_=settings;refreshMonitoring(false);publishWebState();return;}
        }catch(const std::exception& error){MessageBoxW(window_,wide(error.what()).c_str(),L"aeri · ação não aplicada",MB_OK|MB_ICONERROR);}
    }
