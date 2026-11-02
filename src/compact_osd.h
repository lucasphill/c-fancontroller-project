#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace fanctl {
enum CompactOsdMetric : std::uint32_t {
    CompactCpuTemperature=1,CompactCpuUsage=2,CompactGpuTemperature=4,CompactGpuUsage=8,
    CompactCpuRpm=16,CompactGpuRpm=32,CompactFps=64,CompactFrameTime=128
};
struct CompactOsdSettings {
    bool enabled=false;
    std::uint32_t metrics=255;
    bool migratedFromLegacy=false;
};
struct CompactOsdValues {
    std::optional<double> cpuTemperature,cpuUsage,gpuTemperature,gpuUsage,cpuRpm,gpuRpm,fps,frameTimeMs;
};
inline std::string encodeCompactOsdSettings(const CompactOsdSettings& s) {
    if(s.metrics>255)throw std::runtime_error("Selecao de metricas OSD invalida.");
    return "FANCTL_OSD 2\n"+std::to_string(s.enabled?1:0)+" "+std::to_string(s.metrics)+"\n";
}
inline CompactOsdSettings decodeCompactOsdSettings(const std::string& text) {
    std::istringstream input(text);
    std::string magic,extra;int version=0,enabled=-1,corner=-1;long long mask=-1;
    if(!(input>>magic>>version>>enabled) || magic!="FANCTL_OSD" || enabled<0 || enabled>1)
        throw std::runtime_error("Configuracao OSD invalida.");
    if(version==1) {
        if(!(input>>corner>>mask) || corner<0 || corner>3)
            throw std::runtime_error("Configuracao OSD anterior invalida.");
    } else if(version==2) {
        if(!(input>>mask))throw std::runtime_error("Configuracao OSD invalida.");
    } else throw std::runtime_error("Versao de configuracao OSD desconhecida.");
    if(mask<0 || mask>255 || (input>>extra))throw std::runtime_error("Configuracao OSD invalida.");
    // Legacy corner is deliberately discarded: v2 is one top-centred strip.
    return {enabled==1,static_cast<std::uint32_t>(mask),version==1};
}
inline std::filesystem::path compactOsdSettingsPath() {
    wchar_t local[32768]{};const DWORD size=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
    if(!size || size>=32768)throw std::runtime_error("Pasta de configuracao OSD indisponivel.");
    return std::filesystem::path(local)/L"FanController"/L"osd-settings.txt";
}
inline CompactOsdSettings loadCompactOsdSettings() {
    const auto path=compactOsdSettingsPath();std::error_code error;
    if(!std::filesystem::exists(path,error)) {
        if(error)throw std::runtime_error("Falha ao acessar configuracao OSD.");return {};
    }
    const auto bytes=std::filesystem::file_size(path,error);
    if(error || bytes>1024)throw std::runtime_error("Tamanho de configuracao OSD invalido.");
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Falha ao ler configuracao OSD.");
    const std::string text(std::istreambuf_iterator<char>(file),{});
    if(file.bad())throw std::runtime_error("Falha ao ler configuracao OSD.");
    return decodeCompactOsdSettings(text);
}
inline void saveCompactOsdSettings(const CompactOsdSettings& settings) {
    const auto text=encodeCompactOsdSettings(settings);const auto path=compactOsdSettingsPath();
    std::filesystem::create_directories(path.parent_path());auto temporary=path;temporary+=L".tmp";
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Falha ao salvar configuracao OSD.");
    DWORD written=0;const bool saved=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr) &&
        written==text.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if(!saved || !MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());throw std::runtime_error("Falha ao concluir configuracao OSD.");
    }
}
inline std::wstring compactOsdNumber(std::optional<double> value,int precision=0) {
    if(!value || !std::isfinite(*value))return L"—";
    std::wostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(precision)<<*value;
    auto result=out.str();for(auto& ch:result)if(ch==L'.')ch=L',';return result;
}
inline std::wstring formatCompactOsd(const CompactOsdValues& v,const CompactOsdSettings& settings) {
    const auto mask=settings.metrics;std::wstring text;
    const auto selected=[&](CompactOsdMetric metric){return (mask&metric)!=0;};
    const auto group=[&](const std::wstring& value){if(value.empty())return;if(!text.empty())text+=L"  |  ";text+=value;};
    const auto thermal=[&](const wchar_t* label,CompactOsdMetric temperatureMetric,CompactOsdMetric usageMetric,
        std::optional<double> temperature,std::optional<double> usage) {
        if(!selected(temperatureMetric) && !selected(usageMetric))return;
        std::wstring line=label;
        if(selected(temperatureMetric))line+=compactOsdNumber(temperature)+L"°C";
        if(selected(usageMetric)) {if(selected(temperatureMetric))line+=L" · ";line+=compactOsdNumber(usage)+L"%";}
        group(line);
    };
    thermal(L"CPU ",CompactCpuTemperature,CompactCpuUsage,v.cpuTemperature,v.cpuUsage);
    thermal(L"GPU ",CompactGpuTemperature,CompactGpuUsage,v.gpuTemperature,v.gpuUsage);
    if(selected(CompactCpuRpm) && selected(CompactGpuRpm))group(L"Fans "+compactOsdNumber(v.cpuRpm)+L"/"+compactOsdNumber(v.gpuRpm)+L" RPM");
    else if(selected(CompactCpuRpm))group(L"Fan CPU "+compactOsdNumber(v.cpuRpm)+L" RPM");
    else if(selected(CompactGpuRpm))group(L"Fan GPU "+compactOsdNumber(v.gpuRpm)+L" RPM");
    std::wstring frames;
    if(selected(CompactFps))frames=compactOsdNumber(v.fps)+L" FPS";
    if(selected(CompactFrameTime)){if(!frames.empty())frames+=L" · ";frames+=compactOsdNumber(v.frameTimeMs,1)+L" ms";}
    group(frames);return text;
}
} // namespace fanctl
