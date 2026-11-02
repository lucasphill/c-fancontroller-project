#include "../src/monitoring.h"
#include <future>
#include <iostream>
#include <limits>

using namespace fanctl;
namespace {
void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
ControlFrame frame() {
    return {{0, {{50, 3000, 30}, {51, 3200, 40}}, {{1, 65}}}, {GpuSource::Nvml, 60, {}}};
}
void testCpu() {
    CpuUsageCounter counter;
    expect(!counter.sample(100, 200, 100), "First CPU sample has no interval");
    const auto usage = counter.sample(125, 250, 150);
    expect(usage && *usage == 75, "CPU usage must exclude idle from kernel+user delta");
    expect(!counter.sample(125, 250, 150), "Zero interval must not invent zero usage");
    expect(!counter.sample(1, 2, 1), "Counter reset must discard previous baseline");
    expect(counter.sample(101, 102, 1) == 0.0, "A real idle interval must retain zero");
    counter.reset();
    expect(!counter.sample(201, 202, 1), "Failed system reading must reset baseline");
}
void testCache() {
    MonitoringCache cache;
    expect(!cache.snapshot(10).cpuTemperature, "Empty cache has no thermal data");
    cache.submitThermal(frame(), 1000);
    cache.submitUsage({0.0, 100.0, 58}, 1000);
    auto sample = cache.snapshot(4000);
    expect(sample.cpuUsage == 0.0 && sample.gpuUsage == 100.0, "Inclusive usage boundaries lost");
    expect(sample.cpuTemperature == 65 && sample.gpuTemperature == 58, "Independent NVML temperature not selected");
    expect(sample.cpuRpm == 3000 && sample.gpuRpm == 3200, "Fan identity changed");
    sample = cache.snapshot(4001);
    expect(!sample.cpuUsage && !sample.gpuUsage && !sample.gpuTemperature && sample.cpuRpm == 3000,
        "Usage and thermal need independent expiry");
    expect(!cache.snapshot(7001).cpuRpm, "Stale thermal sample reused");
    cache.submitUsage({12.0, 25.0, 59}, 8000);
    cache.submitUsage({std::nullopt, std::nullopt, std::nullopt}, 9000);
    expect(!cache.snapshot(9000).gpuUsage, "Failure must clear last good reading immediately");
    cache.submitUsage({std::numeric_limits<double>::quiet_NaN(), 101.0, 126}, 9001);
    expect(!cache.snapshot(9001).cpuUsage && !cache.snapshot(9001).gpuUsage,
        "Invalid utilization was exposed");
    expect(!cache.snapshot(9001).gpuTemperature, "Invalid NVML temperature exposed");
    auto unvalidated = frame(); unvalidated.gpu.source = GpuSource::Dell;
    cache.submitThermal(unvalidated, 9002);
    expect(!cache.snapshot(9002).gpuTemperature, "Unvalidated Dell GPU sensor leaked into monitoring");
    auto missing = frame();
    missing.snapshot.fans.clear(); missing.snapshot.sensors.clear(); missing.gpu.temperature.reset();
    cache.submitThermal(missing, 10000);
    expect(!cache.snapshot(10000).cpuTemperature && !cache.snapshot(10000).gpuTemperature &&
        !cache.snapshot(10000).cpuRpm, "Missing sensors reused prior readings");
    auto stopped = frame(); stopped.snapshot.fans[0].rpm = 0;
    cache.submitThermal(stopped, 11000);
    expect(cache.snapshot(11000).cpuRpm == 0, "A valid stopped fan is not unavailable");
    expect(!cache.snapshot(10999).cpuRpm, "Time reversal must not freshen data");
}
void testSlowReaderIsolation() {
    std::promise<void> entered, release;
    auto enteredFuture = entered.get_future();
    auto releaseFuture = release.get_future().share();
    MonitoringService service([&] {
        entered.set_value();
        releaseFuture.wait();
        return MonitoringUsage{45.0, 67.0, 58};
    });
    if (enteredFuture.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
        release.set_value(); service.stop();
        throw std::runtime_error("Monitoring worker did not start");
    }
    // An outstanding driver call must not hold the cache lock used by the fan worker.
    auto update = std::async(std::launch::async, [&] {
        service.submitThermal(frame());
        return service.snapshot();
    });
    const bool isolated = update.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
    release.set_value(); service.stop();
    expect(isolated, "Driver acquisition blocked cached thermal handoff");
    expect(update.get().cpuRpm == 3000, "Thermal handoff failed during blocked driver read");
    expect(service.snapshot().gpuUsage == 67.0, "Usage sample not delivered");
    service.stop(); // repeated stop is safe for normal owner lifecycle
}
void testGpuEngineAggregation() {
    const auto identity = L"_luid_0x00000000_0x00001234_";
    auto engine = [&](const wchar_t* process, const wchar_t* suffix, double usage, bool valid = true) {
        return MonitoringEngineValue{std::wstring(process) + identity + suffix, usage, valid};
    };
    std::vector<MonitoringEngineValue> values{
        engine(L"pid_1", L"phys_0_eng_0_engtype_3D", 20),
        engine(L"pid_2", L"phys_0_eng_0_engtype_3D", 30),
        engine(L"pid_1", L"phys_0_eng_1_engtype_Copy", 40),
        {L"pid_1_luid_0x00000000_0x00009999_phys_0_eng_0_engtype_3D", 99, true},
        engine(L"pid_3", L"phys_0_eng_0_engtype_3D", 100, false),
        engine(L"pid_4", L"phys_0_eng_0_engtype_3D", std::numeric_limits<double>::quiet_NaN())
    };
    expect(monitoringBusiestEngine(values, 0, 0x1234) == 50.0,
        "GPU aggregation must sum processes per engine then choose busiest, excluding other adapters");
    expect(!monitoringBusiestEngine(values, 0, 0xabcd), "Missing NVIDIA LUID must not expose Intel utilization");
    values.push_back(engine(L"pid_5", L"phys_0_eng_0_engtype_3D", 80));
    expect(monitoringBusiestEngine(values, 0, 0x1234) == 100.0, "Engine utilization must cap at100");
    expect(!monitoringEngineKey(L"bad_luid_0x00000000_0x00001234_phys_0", 0, 0x1234),
        "Incomplete engine identity must be rejected");
    expect(monitoringBusiestEngine({engine(L"pid_1", L"phys_0_eng_0_engtype_3D", 0)}, 0, 0x1234) == 0.0,
        "A real idle NVIDIA engine must preserve zero");
    MonitoringCache cache;
    cache.submitUsage({10, 40, 55, true}, 1000);
    expect(cache.snapshot(1000).gpuUsageFromWindows, "Windows source attribution lost");
    expect(!cache.snapshot(4001).gpuUsageFromWindows, "Expired usage source must expire as well");
}
}
int main() {
    try {
        testCpu(); testCache(); testSlowReaderIsolation(); testGpuEngineAggregation();
        std::cout << "Monitoring freshness, CPU interval and slow-reader isolation passed (no hardware access).\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
