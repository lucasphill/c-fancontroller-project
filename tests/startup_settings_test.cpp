#include "../src/startup_settings.h"
#include "../src/journal_recovery.h"
#include <iostream>
using namespace fanctl;
namespace {
void expect(bool condition,const char* message) {if (!condition) throw std::runtime_error(message);}
template<class F> void fails(F operation) {bool failed=false;try {operation();} catch (const std::exception&) {failed=true;}expect(failed,"Expected rejection");}
struct Firmware final : ThermalBackend {
    Snapshot state{0,{{50,2000,65},{51,2000,59}}, {}};
    bool rejectSecond=false;
    Snapshot read() override {return state;}
    std::uint32_t readProfile() override {return state.profile;}
    std::uint32_t readBoost(std::uint32_t id) override {return findFan(state,id).boost;}
    void setProfile(std::uint32_t profile) override {state.profile=profile;for (auto& fan : state.fans) fan.boost=0;}
    void setBoost(std::uint32_t id,std::uint32_t boost) override {
        if (rejectSecond && id==51) throw std::runtime_error("Unavailable fan");
        for (auto& fan : state.fans) if (fan.id==id) fan.boost=boost;
    }
};
void tests() {
    for (int mode=0;mode<3;++mode) {
        const StartupSettings settings{mode,100,30,true};
        const auto restored=decodeStartup(encodeStartup(settings));
        expect(restored.mode==mode && restored.first==100 && restored.second==30 && restored.nvml,"Startup round trip failed");
    }
    expect(StartupSettings{}.mode==1,"First install should use Normal");
    fails([] {decodeStartup("FANCTL_STARTUP 2\n1 30 30 0");});
    fails([] {decodeStartup("FANCTL_STARTUP 1\n3 30 30 0");});
    fails([] {decodeStartup("FANCTL_STARTUP 1\n1 101 30 0");});
    fails([] {decodeStartup("FANCTL_STARTUP 1\n1 30 -1 0");});
    fails([] {decodeStartup("FANCTL_STARTUP 1\n1 30 30 2");});
    fails([] {decodeStartup("FANCTL_STARTUP 1\n1 30 30 0 extra");});
    fails([] {decodeStartup("FANCTL_STARTUP 1");});
    const std::string journal="fanctl session v1\nprofile=160\noriginal 50=12\noriginal 51=7\nrequested 50=63\nrequested 51=59\n";
    const auto original=decodeJournal(journal);
    expect(original.profile==160 && findFan(original,50).boost==12 && findFan(original,51).boost==7,"Journal parse failed");
    Firmware firmware;recoverJournal(firmware,original);
    expect(firmware.state.profile==160 && firmware.readBoost(50)==12 && firmware.readBoost(51)==7,"Recovery did not restore profile before both boosts");
    firmware.rejectSecond=true;fails([&] {recoverJournal(firmware,original);});
    fails([&] {decodeJournal(journal+"original 50=10\n");});
    fails([] {decodeJournal("fanctl session v1\nprofile=160\noriginal 50=0\n");});
    fails([] {decodeJournal("fanctl session v1\nprofile=999\noriginal 50=0\noriginal 51=0\n");});
    fails([] {decodeJournal("fanctl session v1\nprofile=160\noriginal 50=256\noriginal 51=0\n");});
}
}
int main() {
    try {tests();std::cout<<"Startup settings and interrupted session recovery checks passed.\n";return 0;}
    catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
