#pragma once
#include "control_session.h"
#include <windows.h>
#include <dxgi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
#include <cwchar>
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "dxgi.lib")

namespace fanctl {
inline std::uint64_t monitoringNowMs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct MonitoringSnapshot {
    std::optional<double> cpuUsage, gpuUsage;
    std::optional<int> cpuTemperature, gpuTemperature, cpuRpm, gpuRpm;
    std::uint64_t sampledAtMs = 0;
    bool gpuUsageFromWindows = false;
};
struct MonitoringUsage {
    std::optional<double> cpuUsage, gpuUsage;
    std::optional<int> gpuTemperature;
    bool gpuUsageFromWindows = false;
};

// GetSystemTimes kernel time includes idle time. First sample establishes a baseline.
class CpuUsageCounter {
public:
    std::optional<double> sample(std::uint64_t idle, std::uint64_t kernel, std::uint64_t user) {
        const bool valid = previous_ && idle >= idle_ && kernel >= kernel_ && user >= user_;
        std::optional<double> result;
        if (valid) {
            const auto total = (kernel - kernel_) + (user - user_);
            const auto inactive = idle - idle_;
            if (total && inactive <= total)
                result = 100.0 * static_cast<double>(total - inactive) / static_cast<double>(total);
        }
        idle_ = idle; kernel_ = kernel; user_ = user; previous_ = true;
        return result;
    }
    void reset() { previous_ = false; }
private:
    std::uint64_t idle_ = 0, kernel_ = 0, user_ = 0;
    bool previous_ = false;
};

// Value-only cache: no driver call or fan-control operation occurs while locked.
class MonitoringCache {
public:
    void submitThermal(const ControlFrame& frame, std::uint64_t now) {
        std::optional<int> cpu, cpuRpm, gpuRpm;
        for (const auto& sensor : frame.snapshot.sensors)
            if (sensor.id == 1 && sensor.temperature > 0 && sensor.temperature <= 125)
                cpu = static_cast<int>(sensor.temperature);
        for (const auto& fan : frame.snapshot.fans) {
            if (fan.id == 50 && fan.rpm <= 20000) cpuRpm = static_cast<int>(fan.rpm);
            if (fan.id == 51 && fan.rpm <= 20000) gpuRpm = static_cast<int>(fan.rpm);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        value_.cpuTemperature = cpu;
        value_.cpuRpm = cpuRpm; value_.gpuRpm = gpuRpm;
        thermalAt_ = now; hasThermal_ = true;
    }
    void submitUsage(MonitoringUsage usage, std::uint64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);
        value_.cpuUsage = validUsage(usage.cpuUsage);
        value_.gpuUsage = validUsage(usage.gpuUsage);
        value_.gpuUsageFromWindows = usage.gpuUsageFromWindows && value_.gpuUsage.has_value();
        value_.gpuTemperature = usage.gpuTemperature;
        if (value_.gpuTemperature && (*value_.gpuTemperature <= 0 || *value_.gpuTemperature > 125))
            value_.gpuTemperature.reset();
        usageAt_ = now; hasUsage_ = true;
    }
    MonitoringSnapshot snapshot(std::uint64_t now) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto result = value_;
        result.sampledAtMs = now;
        if (!hasThermal_ || now < thermalAt_ || now - thermalAt_ > 6000) {
            result.cpuTemperature.reset();
            result.cpuRpm.reset(); result.gpuRpm.reset();
        }
        if (!hasUsage_ || now < usageAt_ || now - usageAt_ > 3000) {
            result.cpuUsage.reset(); result.gpuUsage.reset();
            result.gpuUsageFromWindows = false;
            result.gpuTemperature.reset();
        }
        return result;
    }
private:
    static std::optional<double> validUsage(std::optional<double> value) {
        if (!value || !std::isfinite(*value) || *value < 0 || *value > 100) return std::nullopt;
        return value;
    }
    mutable std::mutex mutex_;
    MonitoringSnapshot value_;
    std::uint64_t thermalAt_ = 0, usageAt_ = 0;
    bool hasThermal_ = false, hasUsage_ = false;
};

class MonitoringNvmlUsage {
public:
    MonitoringNvmlUsage() {
        wchar_t system[MAX_PATH]{};
        if (!GetSystemDirectoryW(system, MAX_PATH)) throw std::runtime_error("Diretorio do sistema indisponivel.");
        const auto path = std::wstring(system) + L"\\nvml.dll";
        library_.handle = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!library_.handle) throw std::runtime_error("NVML indisponivel.");
        const auto initialize = bind<Init>("nvmlInitWithFlags");
        library_.shutdown = bind<Shutdown>("nvmlShutdown");
        const auto count = bind<Count>("nvmlDeviceGetCount_v2");
        const auto handle = bind<Handle>("nvmlDeviceGetHandleByIndex_v2");
        utilization_ = bind<Utilization>("nvmlDeviceGetUtilizationRates");
        temperature_ = bind<Temperature>("nvmlDeviceGetTemperatureV");
        if (initialize(0) != 0) throw std::runtime_error("Inicializar NVML de monitoramento.");
        library_.initialized = true;
        unsigned int devices = 0;
        if (count(&devices) != 0 || devices != 1 || handle(0, &device_) != 0)
            throw std::runtime_error("Monitoramento requer uma GPU NVIDIA identificavel.");
    }
    MonitoringUsage read() {
        MonitoringUsage reading;
        UtilizationValue result{};
        utilizationCode_ = utilization_(device_, &result);
        if (utilizationCode_ == 0 && result.gpu <= 100)
            reading.gpuUsage = static_cast<double>(result.gpu);
        TemperatureV1 temperature{0x0100000c, 0, 0};
        if (temperature_(device_, &temperature) == 0 && temperature.temperature > 0 && temperature.temperature <= 125)
            reading.gpuTemperature = temperature.temperature;
        return reading;
    }
    int utilizationCode() const { return utilizationCode_; }
    MonitoringNvmlUsage(const MonitoringNvmlUsage&) = delete;
    MonitoringNvmlUsage& operator=(const MonitoringNvmlUsage&) = delete;
private:
    using Init = int(__cdecl*)(unsigned int);
    using Shutdown = int(__cdecl*)();
    using Count = int(__cdecl*)(unsigned int*);
    using Handle = int(__cdecl*)(unsigned int, void**);
    struct UtilizationValue { unsigned int gpu, memory; };
    static_assert(sizeof(UtilizationValue) == 8, "NVML utilization ABI mismatch");
    using Utilization = int(__cdecl*)(void*, UtilizationValue*);
    struct TemperatureV1 { unsigned int version; int sensorType; int temperature; };
    static_assert(sizeof(TemperatureV1) == 12, "NVML temperature ABI mismatch");
    using Temperature = int(__cdecl*)(void*, TemperatureV1*);
    struct Library {
        HMODULE handle = nullptr;
        Shutdown shutdown = nullptr;
        bool initialized = false;
        ~Library() {
            if (initialized && shutdown) shutdown();
            if (handle) FreeLibrary(handle);
        }
    } library_;
    void* device_ = nullptr;
    Utilization utilization_ = nullptr;
    Temperature temperature_ = nullptr;
    int utilizationCode_ = -1;
    template<class Function> Function bind(const char* name) {
        const auto address = GetProcAddress(library_.handle, name);
        if (!address) throw std::runtime_error(std::string("Funcao NVML indisponivel: ") + name);
#pragma warning(suppress: 4191)
        return reinterpret_cast<Function>(address);
    }
};

struct MonitoringEngineValue { std::wstring instance; double usage; bool valid = true; };
inline std::optional<std::wstring> monitoringEngineKey(const std::wstring& instance,
    std::uint32_t luidHigh, std::uint32_t luidLow) {
    wchar_t identity[64]{};
    swprintf_s(identity, L"_luid_0x%08x_0x%08x_", luidHigh, luidLow);
    const auto luid = instance.find(identity);
    if (luid == std::wstring::npos) return std::nullopt;
    const auto physical = instance.find(L"phys_", luid + wcslen(identity));
    const auto engine = instance.find(L"_eng_", physical);
    const auto type = instance.find(L"_engtype_", engine);
    if (physical == std::wstring::npos || engine == std::wstring::npos || type == std::wstring::npos)
        return std::nullopt;
    // Process id and engine type are not part of the physical-engine identity.
    return instance.substr(physical, type - physical);
}
inline std::optional<double> monitoringBusiestEngine(const std::vector<MonitoringEngineValue>& values,
    std::uint32_t luidHigh, std::uint32_t luidLow) {
    std::map<std::wstring, double> totals;
    for (const auto& value : values) {
        if (!value.valid || !std::isfinite(value.usage) || value.usage < 0) continue;
        const auto key = monitoringEngineKey(value.instance, luidHigh, luidLow);
        if (key) totals[*key] += value.usage;
    }
    if (totals.empty()) return std::nullopt;
    double busiest = 0;
    for (const auto& engine : totals) if (engine.second > busiest) busiest = engine.second;
    return busiest > 100 ? 100.0 : busiest;
}

// Fallback uses Windows' busiest physical engine, not a sum of unrelated engines.
// DXGI adapter LUID prevents an Intel counter from being labeled NVIDIA usage.
class MonitoringPdhGpuUsage {
public:
    MonitoringPdhGpuUsage() {
        IDXGIFactory1* factory = nullptr;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))))
            throw std::runtime_error("Enumeracao DXGI indisponivel.");
        unsigned int selected = 0;
        for (UINT index = 0;; ++index) {
            IDXGIAdapter1* adapter = nullptr;
            if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) break;
            if (!adapter) break;
            DXGI_ADAPTER_DESC1 description{};
            if (SUCCEEDED(adapter->GetDesc1(&description)) && description.VendorId == 0x10de &&
                !(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                luid_ = description.AdapterLuid;
                ++selected;
            }
            adapter->Release();
        }
        factory->Release();
        if (selected != 1) throw std::runtime_error("Adaptador NVIDIA ambiguo ou ausente.");
        if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS)
            throw std::runtime_error("Consulta GPU Windows indisponivel.");
        if (PdhAddEnglishCounterW(query_, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter_) != ERROR_SUCCESS) {
            PdhCloseQuery(query_); query_ = nullptr;
            throw std::runtime_error("Contadores GPU Windows indisponiveis.");
        }
        PdhCollectQueryData(query_); // interval baseline, never presented as a zero reading
    }
    ~MonitoringPdhGpuUsage() { if (query_) PdhCloseQuery(query_); }
    MonitoringPdhGpuUsage(const MonitoringPdhGpuUsage&) = delete;
    MonitoringPdhGpuUsage& operator=(const MonitoringPdhGpuUsage&) = delete;
    DWORD lastStatus() const { return lastStatus_; }
    DWORD lastCount() const { return lastCount_; }
    DWORD matchingCount() const { return matchingCount_; }
    DWORD validMatchingCount() const { return validMatchingCount_; }
    const std::wstring& firstInstance() const { return firstInstance_; }
    std::uint32_t luidHigh() const { return static_cast<std::uint32_t>(luid_.HighPart); }
    std::uint32_t luidLow() const { return luid_.LowPart; }
    std::optional<double> read() {
        if (!warm_) { warm_ = true; return std::nullopt; }
        lastStatus_ = PdhCollectQueryData(query_);
        if (lastStatus_ != ERROR_SUCCESS) return std::nullopt;
        for (int attempt = 0; attempt < 2; ++attempt) {
            DWORD bytes = 0, count = 0;
            const auto status = PdhGetFormattedCounterArrayW(counter_, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100,
                &bytes, &count, nullptr);
            lastStatus_ = status;
            if (status != PDH_MORE_DATA || bytes == 0 || bytes > 16 * 1024 * 1024) return std::nullopt;
            auto storage = std::make_unique<unsigned char[]>(bytes);
            auto* entries = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(storage.get());
            const auto result = PdhGetFormattedCounterArrayW(counter_, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100,
                &bytes, &count, entries);
            lastStatus_ = result; lastCount_ = count;
            if (result == PDH_MORE_DATA) continue;
            if (result != ERROR_SUCCESS) return std::nullopt;
            std::vector<MonitoringEngineValue> values;
            matchingCount_ = 0; validMatchingCount_ = 0;
            for (DWORD index = 0; index < count; ++index) {
                const auto& entry = entries[index];
                if (index == 0 && entry.szName) firstInstance_ = entry.szName;
                const bool matching = entry.szName && monitoringEngineKey(entry.szName,
                    static_cast<std::uint32_t>(luid_.HighPart), luid_.LowPart).has_value();
                if (matching) ++matchingCount_;
                if (entry.szName && (entry.FmtValue.CStatus == PDH_CSTATUS_VALID_DATA ||
                    entry.FmtValue.CStatus == PDH_CSTATUS_NEW_DATA)) {
                    if (matching) ++validMatchingCount_;
                    values.push_back({entry.szName, entry.FmtValue.doubleValue, true});
                }
            }
            return monitoringBusiestEngine(values, static_cast<std::uint32_t>(luid_.HighPart), luid_.LowPart);
        }
        return std::nullopt;
    }
private:
    LUID luid_{};
    PDH_HQUERY query_ = nullptr;
    PDH_HCOUNTER counter_ = nullptr;
    bool warm_ = false;
    DWORD lastStatus_ = 0, lastCount_ = 0;
    DWORD matchingCount_ = 0, validMatchingCount_ = 0;
    std::wstring firstInstance_;
};

class WindowsMonitoringUsage {
public:
    std::optional<int> gpuUtilizationCode() const {
        return gpu_ ? std::optional<int>(gpu_->utilizationCode()) : std::nullopt;
    }
    MonitoringUsage read() {
        MonitoringUsage value;
        FILETIME idle{}, kernel{}, user{};
        if (GetSystemTimes(&idle, &kernel, &user))
            value.cpuUsage = cpu_.sample(ticks(idle), ticks(kernel), ticks(user));
        else cpu_.reset();
        const auto now = monitoringNowMs();
        if (!gpu_ && now >= retryAt_) {
            retryAt_ = now + 30000;
            try { gpu_ = std::make_unique<MonitoringNvmlUsage>(); } catch (const std::exception&) {}
        }
        if (gpu_) {
            const auto gpu = gpu_->read();
            value.gpuUsage = gpu.gpuUsage;
            value.gpuTemperature = gpu.gpuTemperature;
        }
        if (!value.gpuUsage) {
            if (!windowsGpu_ && now >= windowsRetryAt_) {
                windowsRetryAt_ = now + 30000;
                try { windowsGpu_ = std::make_unique<MonitoringPdhGpuUsage>(); } catch (const std::exception&) {}
            }
            if (windowsGpu_) {
                value.gpuUsage = windowsGpu_->read();
                value.gpuUsageFromWindows = value.gpuUsage.has_value();
            }
        }
        return value;
    }
private:
    static std::uint64_t ticks(const FILETIME& time) {
        return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    }
    CpuUsageCounter cpu_;
    std::unique_ptr<MonitoringNvmlUsage> gpu_;
    std::unique_ptr<MonitoringPdhGpuUsage> windowsGpu_;
    std::uint64_t retryAt_ = 0;
    std::uint64_t windowsRetryAt_ = 0;
};

class MonitoringService {
public:
    using Reader = std::function<MonitoringUsage()>;
    explicit MonitoringService(Reader reader = {}) : reader_(std::move(reader)) {
        if (!reader_) {
            auto source = std::make_shared<WindowsMonitoringUsage>();
            reader_ = [source] { return source->read(); };
        }
        worker_ = std::thread([this] { run(); });
    }
    ~MonitoringService() { stop(); }
    MonitoringService(const MonitoringService&) = delete;
    MonitoringService& operator=(const MonitoringService&) = delete;
    void submitThermal(const ControlFrame& frame) { cache_.submitThermal(frame, monitoringNowMs()); }
    MonitoringSnapshot snapshot() const { return cache_.snapshot(monitoringNowMs()); }
    void stop() {
        { std::lock_guard<std::mutex> lock(stopMutex_); stopping_ = true; }
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
private:
    void run() {
        std::unique_lock<std::mutex> lock(stopMutex_);
        while (!stopping_) {
            lock.unlock();
            // Stamp before acquisition: a slow driver must not freshen an old sample.
            const auto started = monitoringNowMs();
            MonitoringUsage usage;
            try { usage = reader_(); } catch (const std::exception&) {}
            cache_.submitUsage(usage, started);
            lock.lock();
            wake_.wait_until(lock, std::chrono::steady_clock::time_point(std::chrono::milliseconds(started + 1000)),
                [this] { return stopping_; });
        }
    }
    MonitoringCache cache_;
    Reader reader_;
    std::mutex stopMutex_;
    std::condition_variable wake_;
    bool stopping_ = false;
    std::thread worker_;
};
} // namespace fanctl
