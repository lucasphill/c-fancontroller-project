#pragma once
#include "control_session.h"
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>

namespace fanctl {
struct CurvePoint { int temperature; int boost; };
using Curve=std::array<CurvePoint,5>;
using ProfileCurves=std::array<Curve,2>;
using CurveSettings=std::array<ProfileCurves,2>;
inline CurveSettings defaultCurves() {
    CurveSettings settings{};
    const int boosts[]{30,45,65,85,100};
    for (int profile=0;profile<2;++profile) for (int fan=0;fan<2;++fan)
        for (int point=0;point<5;++point)
            settings[profile][fan][point]={50-fan*10-profile*10+point*10,(profile==0 && point==0) ? 20 : boosts[point]};
    return settings;
}
inline void validateCurves(const CurveSettings& settings) {
    for (std::size_t profile=0;profile<settings.size();++profile) for (const auto& curve : settings[profile]) {
        for (std::size_t i=0;i<curve.size();++i) {
            const auto& point=curve[i];
            if (point.temperature<1 || point.temperature>125 || point.boost<(profile==0 ? 20 : 30) || point.boost>100 ||
                (i && (point.temperature<=curve[i-1].temperature || point.boost<curve[i-1].boost)))
                throw std::runtime_error("Curva invalida: temperaturas crescentes (1..125 C), boosts nao decrescentes (Normal 20..100; Performance 30..100).");
        }
    }
}
inline std::string encodeCurves(const CurveSettings& settings) {
    validateCurves(settings);std::ostringstream out;out<<"FANCTL_CURVES 1\n";
    for (const auto& profile : settings) for (const auto& curve : profile)
        for (const auto& point : curve) out<<point.temperature<<' '<<point.boost<<'\n';
    return out.str();
}
inline CurveSettings decodeCurves(const std::string& text) {
    std::istringstream in(text);std::string signature;int version=0;
    if (!(in>>signature>>version) || signature!="FANCTL_CURVES" || version!=1)
        throw std::runtime_error("Arquivo de curvas invalido.");
    CurveSettings settings{};
    for (auto& profile : settings) for (auto& curve : profile) for (auto& point : curve)
        if (!(in>>point.temperature>>point.boost)) throw std::runtime_error("Arquivo de curvas incompleto.");
    std::string extra;if (in>>extra) throw std::runtime_error("Dados extras no arquivo de curvas.");
    validateCurves(settings);return settings;
}
inline double curveBoost(const Curve& curve,double temperature) {
    if (temperature<=curve.front().temperature) return curve.front().boost;
    for (std::size_t i=1;i<curve.size();++i) if (temperature<=curve[i].temperature) {
        const auto& a=curve[i-1];const auto& b=curve[i];
        return a.boost+(temperature-a.temperature)*(b.boost-a.boost)/(b.temperature-a.temperature);
    }
    return curve.back().boost;
}
inline std::array<int,2> automaticTemperatures(const ControlFrame& frame) {
    int cpu=0;
    for (const auto& sensor : frame.snapshot.sensors) if (sensor.id==1 && sensor.temperature<=125)
        cpu=static_cast<int>(sensor.temperature);
    if (cpu<1 || !frame.gpu.temperature || frame.gpu.source!=GpuSource::Nvml ||
        *frame.gpu.temperature<1 || *frame.gpu.temperature>125)
        throw std::runtime_error("Temperatura necessaria ao perfil automatico indisponivel ou fora de 1..125 C.");
    return {cpu,*frame.gpu.temperature};
}
class AutomaticPolicy {
public:
    using Clock=std::chrono::steady_clock;
    explicit AutomaticPolicy(ProfileCurves curves) : curves_(std::move(curves)) {}
    void replaceCurves(ProfileCurves curves) {
        curves_=std::move(curves);
        // Keep zero-RPM history when editing an active profile.
        lower_={};initialized_=false;
    }
    std::vector<Adjustment> next(const ControlFrame& frame,Clock::time_point now) {
        const auto temperatures=automaticTemperatures(frame);
        std::vector<Adjustment> targets;
        for (std::size_t i=0;i<2;++i) {
            const auto id=static_cast<std::uint32_t>(50+i);
            if (findFan(frame.snapshot,id).rpm==0) {
                if (!zero_[i]) zero_[i]=now;
                if (now-*zero_[i]>=std::chrono::seconds(10))
                    throw std::runtime_error("Fan "+std::to_string(id)+" indica 0 RPM ha 10 segundos.");
            } else zero_[i].reset();
            const int target=static_cast<int>(std::ceil(curveBoost(curves_[i],temperatures[i])));
            if (!initialized_ || target>=applied_[i]) {applied_[i]=target;lower_[i].reset();}
            else if (curveBoost(curves_[i],temperatures[i]+3)<=applied_[i]) {
                if (!lower_[i]) lower_[i]=now;
                if (now-*lower_[i]>=std::chrono::seconds(10)) {applied_[i]=target;lower_[i].reset();}
            } else lower_[i].reset();
            targets.push_back({id,static_cast<std::uint32_t>(applied_[i])});
        }
        initialized_=true;return targets;
    }
private:
    ProfileCurves curves_;
    bool initialized_=false;
    std::array<int,2> applied_{};
    std::array<std::optional<Clock::time_point>,2> lower_{},zero_{};
};
} // namespace fanctl
