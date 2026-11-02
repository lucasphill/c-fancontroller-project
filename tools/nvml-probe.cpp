// Read-only experiment; ABI signatures from NVIDIA's NVML API reference.
// No SDK installation, link-time dependency, or device setter is used.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using Clock = std::chrono::steady_clock;
using Device = void*;
using Init = int(__cdecl*)(unsigned int);
using Shutdown = int(__cdecl*)();
using Count = int(__cdecl*)(unsigned int*);
using Handle = int(__cdecl*)(unsigned int, Device*);
using Temperature = int(__cdecl*)(Device, unsigned int, unsigned int*);
using Power = int(__cdecl*)(Device, unsigned int*);
using Pstate = int(__cdecl*)(Device, int*);
struct TemperatureV1 { unsigned int version; int sensorType; int temperature; };
static_assert(sizeof(TemperatureV1) == 12, "NVML temperature ABI mismatch");
using TemperatureV = int(__cdecl*)(Device, TemperatureV1*);

struct Library {
    HMODULE module = nullptr;
    Shutdown shutdown = nullptr;
    bool initialized = false;
    ~Library() {
        if (initialized && shutdown) shutdown();
        if (module) FreeLibrary(module);
    }
    template<class T> T bind(const char* name) {
        const auto address = GetProcAddress(module, name);
        if (!address) throw std::runtime_error(std::string("NVML symbol absent: ") + name);
#pragma warning(suppress: 4191)
        return reinterpret_cast<T>(address);
    }
};
double milliseconds(Clock::duration value) {
    return std::chrono::duration<double, std::milli>(value).count();
}
double cpuMilliseconds() {
    FILETIME start{}, end{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &start, &end, &kernel, &user))
        throw std::runtime_error("Process CPU time unavailable");
    ULARGE_INTEGER k{}, u{};
    k.LowPart=kernel.dwLowDateTime; k.HighPart=kernel.dwHighDateTime;
    u.LowPart=user.dwLowDateTime; u.HighPart=user.dwHighDateTime;
    return static_cast<double>(k.QuadPart + u.QuadPart) / 10000.0;
}
void timestamp(char* target, std::size_t size) {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::snprintf(target, size, "%04u-%02u-%02u %02u:%02u:%02u.%03u",
        time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds);
}
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: nvml-probe idle|init|temperature|telemetry|modern metrics.json\n";
        return 2;
    }
    const std::string mode(argv[1]);
    if (mode != "idle" && mode != "init" && mode != "temperature" && mode != "telemetry" && mode != "modern") return 2;
    try {
        const auto started = Clock::now();
        Library library;
        Device device = nullptr;
        Temperature getTemperature = nullptr;
        TemperatureV getTemperatureV = nullptr;
        Power getPower = nullptr;
        Pstate getPstate = nullptr;
        if (mode != "idle") {
            wchar_t systemPath[MAX_PATH]{};
            if (!GetSystemDirectoryW(systemPath, MAX_PATH)) throw std::runtime_error("System path unavailable");
            const std::wstring path = std::wstring(systemPath) + L"\\nvml.dll";
            library.module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!library.module) throw std::runtime_error("Installed system NVML DLL unavailable");
            const auto init = library.bind<Init>("nvmlInitWithFlags");
            library.shutdown = library.bind<Shutdown>("nvmlShutdown");
            const int result = init(mode == "init" ? 2u : 0u); // NO_ATTACH only for the initialization-only experiment.
            if (result != 0) throw std::runtime_error("NVML initialization failed, code " + std::to_string(result));
            library.initialized = true;
            if (mode != "init") {
                const auto count = library.bind<Count>("nvmlDeviceGetCount_v2");
                unsigned int devices = 0;
                const int countResult = count(&devices);
                if (countResult != 0 || devices != 1)
                    throw std::runtime_error("GPU enumeration code " + std::to_string(countResult) + ", count " + std::to_string(devices));
                const auto handle = library.bind<Handle>("nvmlDeviceGetHandleByIndex_v2");
                const int handleResult = handle(0, &device);
                if (handleResult != 0) throw std::runtime_error("GPU handle failed, code " + std::to_string(handleResult));
                // This older temperature function is supported by the installed driver;
                // NVIDIA now marks it deprecated in favor of GetTemperatureV.
                getTemperature = library.bind<Temperature>("nvmlDeviceGetTemperature");
                if (mode == "modern") getTemperatureV = library.bind<TemperatureV>("nvmlDeviceGetTemperatureV");
                if (mode == "telemetry") {
                    getPower = library.bind<Power>("nvmlDeviceGetPowerUsage");
                    getPstate = library.bind<Pstate>("nvmlDeviceGetPowerState");
                }
            }
        }
        const double initMs = milliseconds(Clock::now() - started);
        double queryTotalMs = 0;
        int successes = 0;
        int modernSuccesses = 0;
        std::cout << "time,mode,index,tempCode,temperatureC,powerCode,powerMilliwatts,pstateCode,pstate,queryMs,modernCode,modernTemperatureC\n";
        for (int index = 0; index < 15; ++index) {
            char time[48]{};
            timestamp(time, sizeof(time));
            unsigned int temperature = 0, power = 0;
            int pstate = 0;
            int tempCode = -1, powerCode = -1, pstateCode = -1;
            int modernCode = -1;
            TemperatureV1 modern{0x0100000c, 0, 0};
            const auto queryStart = Clock::now();
            if (getTemperature) tempCode = getTemperature(device, 0, &temperature); // NVML_TEMPERATURE_GPU.
            if (getTemperatureV) modernCode = getTemperatureV(device, &modern);
            if (getPower) powerCode = getPower(device, &power);
            if (getPstate) pstateCode = getPstate(device, &pstate);
            const double queryMs = milliseconds(Clock::now() - queryStart);
            queryTotalMs += queryMs;
            if (tempCode == 0) ++successes;
            if (modernCode == 0) ++modernSuccesses;
            std::cout << time << ',' << mode << ',' << index << ',' << tempCode << ',';
            if (tempCode == 0) std::cout << temperature;
            std::cout << ',' << powerCode << ',';
            if (powerCode == 0) std::cout << power;
            std::cout << ',' << pstateCode << ',';
            if (pstateCode == 0) std::cout << pstate;
            std::cout << ',' << queryMs << ',' << modernCode << ',';
            if (modernCode == 0) std::cout << modern.temperature;
            std::cout << '\n' << std::flush;
            Sleep(2000);
        }
        PROCESS_MEMORY_COUNTERS memory{};
        memory.cb=sizeof(memory);
        if (!GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)))
            throw std::runtime_error("Process memory unavailable");
        std::ofstream report(argv[2]);
        if (!report) throw std::runtime_error("Cannot create metrics report");
        report << "{\"mode\":\"" << mode << "\",\"samples\":15,\"temperatureSuccesses\":" << successes
            << ",\"modernSuccesses\":" << modernSuccesses
            << ",\"initializationMs\":" << initMs << ",\"meanQueryMs\":" << queryTotalMs/15
            << ",\"durationMs\":" << milliseconds(Clock::now()-started)
            << ",\"cpuTimeMsBeforeCleanup\":" << cpuMilliseconds()
            << ",\"workingSetBytes\":" << memory.WorkingSetSize
            << ",\"peakWorkingSetBytes\":" << memory.PeakWorkingSetSize << "}\n";
        report.flush();
        if (!report) throw std::runtime_error("Cannot persist metrics report");
        return getTemperature && successes != 15 ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
