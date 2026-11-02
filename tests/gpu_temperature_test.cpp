#include "../src/cli.h"
#include <iostream>

using namespace fanctl;
namespace {
void expect(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class Function> void expectFailure(Function operation) {
    bool failed=false;
    try { operation(); } catch (const std::exception&) { failed=true; }
    expect(failed,"Expected invalid options to fail");
}
struct State { int value=55; int reads=0; bool fail=false; };
class FakeReader final : public NvidiaTemperatureSource {
public:
    explicit FakeReader(State& state) : state_(state) {}
    int read() override {
        ++state_.reads;
        if (state_.fail) throw std::runtime_error("device lost");
        return state_.value;
    }
private: State& state_;
};
void tests() {
    {
        int factoryCalls=0;
        GpuMonitor monitor(GpuSource::Dell,[&]() -> std::unique_ptr<NvidiaTemperatureSource> {
            ++factoryCalls; throw std::runtime_error("NVML must stay off");
        });
        expect(monitor.read(30).temperature == 30,"Default Dell reading changed");
        expect(monitor.read(55).source == GpuSource::Dell,"Default source mislabeled");
        expect(factoryCalls == 0,"Default mode initialized NVML");
    }
    {
        State state;
        int factoryCalls=0;
        GpuMonitor monitor(GpuSource::Nvml,[&] { ++factoryCalls;return std::make_unique<FakeReader>(state); });
        expect(factoryCalls == 0,"NVML loaded before first request");
        const auto value=monitor.read(30);
        expect(value.source == GpuSource::Nvml && value.temperature == 55,"NVML did not override selected GPU source");
        state.value=56;
        expect(monitor.read(30).temperature == 56,"NVML readings were not refreshed");
        expect(factoryCalls == 1 && state.reads == 2,"NVML was reinitialized per sample");
    }
    {
        int factoryCalls=0;
        GpuMonitor monitor(GpuSource::Nvml,[&]() -> std::unique_ptr<NvidiaTemperatureSource> {
            ++factoryCalls;throw std::runtime_error("DLL unavailable");
        });
        const auto value=monitor.read(60);
        expect(!value.temperature && value.source == GpuSource::Nvml,"Initialization failure silently fell back to Dell");
        expect(value.error == "DLL unavailable","Initialization error hidden");
        expect(!monitor.read(61).temperature && factoryCalls == 1,"Repeated initialization after startup failure");
    }
    {
        State state;
        GpuMonitor monitor(GpuSource::Nvml,[&] {return std::make_unique<FakeReader>(state);});
        expect(monitor.read(30).temperature == 55,"Initial GPU read failed");
        state.fail=true;
        const auto failed=monitor.read(60);
        expect(!failed.temperature && failed.error == "device lost","Read failure reused stale GPU or Dell temperature");
        state.fail=false;state.value=57;
        expect(monitor.read(30).temperature == 57,"Later valid read did not recover");
    }
    {
        State state;
        GpuMonitor monitor(GpuSource::Nvml,[&] {return std::make_unique<FakeReader>(state);});
        for (const auto invalid : {-1,0,126}) {
            state.value=invalid;
            expect(!monitor.read(55).temperature,"Implausible NVML value treated as valid");
        }
        state.value=30;
        expect(monitor.read(55).temperature == 30,"Valid NVML 30 was globally blacklisted");
    }
    {
        expect(parseOptions({"status"}).gpuSource == GpuSource::Dell,"Default CLI enabled NVML");
        expect(parseOptions({"watch","--gpu-source","nvml"}).gpuSource == GpuSource::Nvml,"Watch NVML not parsed");
        const auto options=parseOptions({"boost","--fan","50=100","--gpu-source","nvml","--fan","51=75"});
        expect(options.adjustments.size() == 2 && options.gpuSource == GpuSource::Nvml,"Interleaved boost options failed");
        expect(parseOptions({"status","--gpu-source","dell"}).gpuSource == GpuSource::Dell,"Explicit Dell source failed");
        expect(parseOptions({}).command == Command::Help,"Empty args did not show help");
    }
    {
        for (const auto& args : std::vector<std::vector<std::string>>{
            {"status","--gpu-source"}, {"watch","--gpu-source","auto"},
            {"status","--gpu-source","dell","--gpu-source","nvml"},
            {"status","--fan","50=30"}, {"boost","--gpu-source","nvml"},
            {"boost","--fan","50=101"}, {"boost","--fan","50=30","--fan","50=20"},
            {"watch","--other","nvml"}}) expectFailure([&] {parseOptions(args);});
    }
}
}
int main() {
    try {tests();std::cout << "GPU source and CLI scenarios passed (no hardware access).\n";return 0;}
    catch (const std::exception& error) {std::cerr << error.what() << '\n';return 1;}
}
