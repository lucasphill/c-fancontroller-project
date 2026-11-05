#include "../src/automatic_profiles.h"
#include <iostream>
using namespace fanctl;
namespace {
void expect(bool condition,const char* message) {if (!condition) throw std::runtime_error(message);}
template<class F> void fails(F operation) {
    bool failed=false;try {operation();} catch (const std::exception&) {failed=true;}
    expect(failed,"Expected rejection");
}
ControlFrame frame(int cpu=60,int gpu=50,int rpm=2000) {
    return {{0,{{50,static_cast<std::uint32_t>(rpm),30},{51,2000,30}},{{1,static_cast<std::uint32_t>(cpu)}}},
        {GpuSource::Nvml,gpu,{}}};
}
void tests() {
    auto settings=defaultCurves();validateCurves(settings);
    expect(settings[0][0][0].boost==20 && settings[0][1][0].boost==20,"Normal floor not 20");
    expect(settings[1][0][0].boost==30 && settings[1][1][0].boost==30,"Performance floor changed");
    auto performance=settings;performance[1][0][0].boost=20;fails([&] {validateCurves(performance);});
    AutomaticPolicy cool(settings[0]);
    const auto low=cool.next(frame(40,30),AutomaticPolicy::Clock::time_point{});
    expect(low[0].boost==20 && low[1].boost==20,"Normal did not request 20 at low temperatures");
    expect(encodeCurves(decodeCurves(encodeCurves(settings)))==encodeCurves(settings),"Settings did not round trip");
    std::ostringstream legacy;legacy<<"FANCTL_CURVES 1\n";
    for(const auto& p:settings)for(const auto& c:p)for(const auto& point:c)legacy<<point.temperature<<' '<<point.boost<<'\n';
    expect(encodeCurves(decodeCurves(legacy.str()))==encodeCurves(settings),"Legacy profiles must migrate without changing points");
    auto variable=settings;variable[0][0].insert(variable[0][0].begin()+1,{55,30});
    expect(decodeCurves(encodeCurves(variable))[0][0].size()==6,"Variable point count must persist");
    variable[0][0].resize(1);fails([&]{validateCurves(variable);});
    fails([] {decodeCurves("FANCTL_CURVES 2");});
    fails([&] {decodeCurves(encodeCurves(settings)+"unexpected");});
    fails([&] {decodeCurves(encodeCurves(settings).substr(0,30));});
    auto invalid=settings;invalid[0][0][1].temperature=50;fails([&] {validateCurves(invalid);});
    invalid=settings;invalid[0][0][0].boost=19;fails([&] {validateCurves(invalid);});
    invalid=settings;invalid[0][0][1].boost=101;fails([&] {validateCurves(invalid);});
    invalid=settings;invalid[0][0][2].boost=40;fails([&] {validateCurves(invalid);});
    expect(curveBoost(settings[0][0],-5)==20 && curveBoost(settings[0][0],130)==100,"Endpoints not clamped");
    expect(curveBoost(settings[0][0],65)==55,"Interpolation incorrect");
    expect(curveBoost(settings[1][0],80)==100 && curveBoost(settings[1][1],70)==100,"Performance not advanced");
    const auto start=AutomaticPolicy::Clock::time_point{};
    AutomaticPolicy policy(settings[0]);
    expect(policy.next(frame(),start)[0].boost==45,"Wrong initial target");
    expect(policy.next(frame(80,70),start+std::chrono::seconds(2))[0].boost==85,"Rise was delayed");
    expect(policy.next(frame(78,68),start+std::chrono::seconds(4))[0].boost==85,"Three degree margin ignored");
    expect(policy.next(frame(70,60),start+std::chrono::seconds(6))[0].boost==85,"Drop was immediate");
    expect(policy.next(frame(70,60),start+std::chrono::seconds(14))[0].boost==85,"Drop too early");
    expect(policy.next(frame(79,69),start+std::chrono::seconds(15))[0].boost==85,"Unexpected drop");
    policy.next(frame(70,60),start+std::chrono::seconds(16));
    expect(policy.next(frame(70,60),start+std::chrono::seconds(24))[0].boost==85,"Countdown did not reset");
    expect(policy.next(frame(70,60),start+std::chrono::seconds(26))[0].boost==65,"Sustained cooling did not lower boost");
    expect(policy.next(frame(80,40),start+std::chrono::seconds(28))[0].boost==85,"CPU curve did not rise independently");
    AutomaticPolicy stopped(settings[0]);
    stopped.next(frame(60,50,0),start);
    stopped.next(frame(60,50,0),start+std::chrono::seconds(8));
    stopped.replaceCurves(settings[1]);
    fails([&] {stopped.next(frame(60,50,0),start+std::chrono::seconds(10));});
    AutomaticPolicy recovered(settings[0]);
    recovered.next(frame(60,50,0),start);
    recovered.next(frame(),start+std::chrono::seconds(8));
    recovered.next(frame(60,50,0),start+std::chrono::seconds(10));
    recovered.next(frame(60,50,0),start+std::chrono::seconds(18));
    auto missing=frame();missing.gpu.temperature.reset();fails([&] {automaticTemperatures(missing);});
    missing=frame();missing.snapshot.sensors.clear();fails([&] {automaticTemperatures(missing);});
    missing=frame(0);fails([&] {automaticTemperatures(missing);});
    missing=frame(126);fails([&] {automaticTemperatures(missing);});
    missing=frame();missing.gpu.source=GpuSource::Dell;fails([&] {automaticTemperatures(missing);});
    expect(automaticTemperatures(frame(60,30))[1]==30,"Valid NVML 30 was rejected");
}
}
int main() {
    try {tests();std::cout<<"Automatic curves, hysteresis, RPM and configuration checks passed.\n";return 0;}
    catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
