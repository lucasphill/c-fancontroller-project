#pragma once
#include <sstream>
#include <stdexcept>
#include <string>
namespace fanctl {
struct StartupSettings {
    int mode=1,first=30,second=30;
    bool nvml=false;
};
inline void validateStartup(const StartupSettings& settings) {
    if (settings.mode<0 || settings.mode>2 || settings.first<0 || settings.first>100 ||
        settings.second<0 || settings.second>100)
        throw std::runtime_error("Configuracao de inicio invalida.");
}
inline std::string encodeStartup(const StartupSettings& settings) {
    validateStartup(settings);std::ostringstream out;
    out<<"FANCTL_STARTUP 1\n"<<settings.mode<<' '<<settings.first<<' '<<settings.second<<' '<<settings.nvml<<'\n';
    return out.str();
}
inline StartupSettings decodeStartup(const std::string& text) {
    std::istringstream in(text);std::string signature;int version=0,nvml=0;StartupSettings settings;
    if (!(in>>signature>>version>>settings.mode>>settings.first>>settings.second>>nvml) ||
        signature!="FANCTL_STARTUP" || version!=1 || (nvml!=0 && nvml!=1))
        throw std::runtime_error("Arquivo de inicio invalido.");
    std::string extra;if (in>>extra) throw std::runtime_error("Dados extras na configuracao de inicio.");
    settings.nvml=nvml!=0;validateStartup(settings);return settings;
}
} // namespace fanctl
