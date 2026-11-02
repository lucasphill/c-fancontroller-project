#pragma once
#include "gpu_temperature.h"
#include <windows.h>

namespace fanctl {
class NvmlTemperature final : public NvidiaTemperatureSource {
public:
    NvmlTemperature() {
        wchar_t system[MAX_PATH]{};
        if (!GetSystemDirectoryW(system, MAX_PATH)) throw std::runtime_error("Diretorio do sistema indisponivel.");
        const std::wstring path = std::wstring(system) + L"\\nvml.dll";
        library_.handle = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!library_.handle) throw std::runtime_error("nvml.dll do driver NVIDIA indisponivel.");
        const auto initialize = bind<Init>("nvmlInitWithFlags");
        library_.shutdown = bind<Shutdown>("nvmlShutdown");
        const auto count = bind<Count>("nvmlDeviceGetCount_v2");
        const auto handle = bind<Handle>("nvmlDeviceGetHandleByIndex_v2");
        temperature_ = bind<Temperature>("nvmlDeviceGetTemperatureV");
        require(initialize(0), "Inicializar NVML");
        library_.initialized = true;
        unsigned int devices = 0;
        require(count(&devices), "Enumerar GPU NVIDIA");
        if (devices != 1) throw std::runtime_error("Esperada uma GPU NVIDIA; nenhuma selecao automatica foi feita.");
        require(handle(0, &device_), "Acessar GPU NVIDIA");
    }
    int read() override {
        TemperatureV1 value{0x0100000c, 0, 0};
        require(temperature_(device_, &value), "Ler temperatura NVML");
        return value.temperature;
    }
    NvmlTemperature(const NvmlTemperature&) = delete;
    NvmlTemperature& operator=(const NvmlTemperature&) = delete;
private:
    using Device = void*;
    using Init = int(__cdecl*)(unsigned int);
    using Shutdown = int(__cdecl*)();
    using Count = int(__cdecl*)(unsigned int*);
    using Handle = int(__cdecl*)(unsigned int, Device*);
    // ABI checked against NVIDIA nvmlTemperature_v1_t; no SDK runtime is required.
    struct TemperatureV1 { unsigned int version; int sensorType; int temperature; };
    static_assert(sizeof(TemperatureV1) == 12, "NVML ABI mismatch");
    using Temperature = int(__cdecl*)(Device, TemperatureV1*);
    struct Library {
        HMODULE handle = nullptr;
        Shutdown shutdown = nullptr;
        bool initialized = false;
        ~Library() {
            if (initialized && shutdown) shutdown();
            if (handle) FreeLibrary(handle);
        }
    } library_;
    Device device_ = nullptr;
    Temperature temperature_ = nullptr;
    template<class Function> Function bind(const char* name) {
        const auto address = GetProcAddress(library_.handle, name);
        if (!address) throw std::runtime_error(std::string("Funcao NVML indisponivel: ") + name);
#pragma warning(suppress: 4191)
        return reinterpret_cast<Function>(address);
    }
    static void require(int code, const char* operation) {
        if (code != 0) throw std::runtime_error(std::string(operation) + ": codigo " + std::to_string(code));
    }
};
} // namespace fanctl
