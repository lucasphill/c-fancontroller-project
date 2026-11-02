// Explicit read-only hardware probe; excluded from the hardware-free unit suite.
#include "../src/monitoring.h"
#include <iostream>

int main() {
    fanctl::WindowsMonitoringUsage source;
    source.read(); // CPU interval baseline
    std::this_thread::sleep_for(std::chrono::seconds(1));
    const auto started = fanctl::monitoringNowMs();
    const auto sample = source.read();
    std::cout << "{\"cpu_usage\":";
    if (sample.cpuUsage) std::cout << *sample.cpuUsage; else std::cout << "null";
    std::cout << ",\"gpu_usage\":";
    if (sample.gpuUsage) std::cout << *sample.gpuUsage; else std::cout << "null";
    std::cout << ",\"gpu_temperature\":";
    if (sample.gpuTemperature) std::cout << *sample.gpuTemperature; else std::cout << "null";
    std::cout << ",\"gpu_utilization_return_code\":";
    if (source.gpuUtilizationCode()) std::cout << *source.gpuUtilizationCode(); else std::cout << "null";
    std::cout << ",\"gpu_usage_source\":\"" << (!sample.gpuUsage ? "unavailable" :
        sample.gpuUsageFromWindows ? "Windows busiest NVIDIA engine" : "NVML") << "\"";
    std::cout << ",\"acquisition_ms\":" << fanctl::monitoringNowMs() - started << "}\n";
    try {
        fanctl::MonitoringPdhGpuUsage pdh;
        pdh.read();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const auto usage = pdh.read();
        std::cout << "{\"pdh_status\":" << pdh.lastStatus() << ",\"count\":" << pdh.lastCount()
            << ",\"matching_count\":" << pdh.matchingCount() << ",\"valid_matching_count\":" << pdh.validMatchingCount()
            << ",\"luid_high\":" << pdh.luidHigh() << ",\"luid_low\":" << pdh.luidLow() << ",\"usage\":";
        if (usage) std::cout << *usage; else std::cout << "null";
        std::cout << "}\n";
        std::wcerr << L"PDH instance example: " << pdh.firstInstance() << L'\n';
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    return sample.cpuUsage ? 0 : 1;
}
