#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace fanctl {
enum class GpuSource { Dell, Nvml };
struct GpuReading {
    GpuSource source;
    std::optional<int> temperature;
    std::string error;
};
class NvidiaTemperatureSource {
public:
    virtual ~NvidiaTemperatureSource() = default;
    virtual int read() = 0;
};

// NVML is created on first explicit request only. Errors never substitute Dell data.
class GpuMonitor {
public:
    using Factory = std::function<std::unique_ptr<NvidiaTemperatureSource>()>;
    GpuMonitor(GpuSource source, Factory factory) : source_(source), factory_(std::move(factory)) {}
    GpuReading read(std::optional<int> dellTemperature) {
        if (source_ == GpuSource::Dell) return {source_, dellTemperature, {}};
        try {
            if (!attempted_) {
                attempted_ = true;
                try {
                    reader_ = factory_();
                    if (!reader_) throw std::runtime_error("Fonte NVML vazia.");
                } catch (const std::exception& error) { initializationError_ = error.what(); }
            }
            if (!reader_) return {source_, std::nullopt, initializationError_};
            const int temperature = reader_->read();
            if (temperature <= 0 || temperature > 125)
                return {source_, std::nullopt, "Temperatura NVML fora do intervalo plausivel."};
            return {source_, temperature, {}};
        } catch (const std::exception& error) { return {source_, std::nullopt, error.what()}; }
    }
    GpuSource source() const { return source_; }
private:
    GpuSource source_;
    Factory factory_;
    std::unique_ptr<NvidiaTemperatureSource> reader_;
    bool attempted_ = false;
    std::string initializationError_;
};
} // namespace fanctl
