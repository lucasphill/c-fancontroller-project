#pragma once
#include "boost_session.h"
#include "gpu_temperature.h"

namespace fanctl {
struct ControlFrame { Snapshot snapshot; GpuReading gpu; };
inline GpuReading selectedGpu(const Snapshot& snapshot, GpuMonitor& monitor) {
    std::optional<int> dell;
    for (const auto& sensor : snapshot.sensors)
        if (sensor.id == 6 && sensor.temperature <= 125) dell = static_cast<int>(sensor.temperature);
    return monitor.read(dell);
}
class SessionResources {
public:
    virtual ~SessionResources() = default;
    virtual void save(const Snapshot& original, const std::vector<Adjustment>& requests) = 0;
    virtual void clear() = 0;
};

// Shared by CLI and GUI: one owner for acquisition, journaling and rollback policy.
class ControlSession {
public:
    ControlSession(ThermalBackend& backend, GpuMonitor& monitor,
        std::vector<Adjustment> requests, std::unique_ptr<SessionResources> resources)
        : resources_(std::move(resources)), session_(backend, requests), monitor_(monitor) {
        if (!resources_) throw std::runtime_error("Recursos da sessao indisponiveis.");
        requireGpu(selectedGpu(session_.original(), monitor_));
        resources_->save(session_.original(), requests);
    }
    void apply(const std::function<bool()>& stopping) { session_.apply(stopping); }
    void update(const std::vector<Adjustment>& requests, const std::function<bool()>& stopping) {
        session_.update(requests,stopping);
    }
    ControlFrame sample() {
        auto snapshot = session_.sample();
        auto gpu = selectedGpu(snapshot, monitor_);
        requireGpu(gpu);
        return {std::move(snapshot), std::move(gpu)};
    }
    RestoreResult restore() {
        if (restored_) return {true,{"Estado anterior ja confirmado."}};
        auto result = session_.restore();
        if (result.verified) {
            try { resources_->clear(); restored_ = true; }
            catch (const std::exception& error) {
                result.verified = false;
                result.messages.push_back(std::string("Estado restaurado; registro pendente: ") + error.what());
            }
        }
        return result;
    }
private:
    static void requireGpu(const GpuReading& gpu) {
        if (gpu.source == GpuSource::Nvml && !gpu.temperature)
            throw std::runtime_error("GPU NVML indisponivel: " + gpu.error);
    }
    std::unique_ptr<SessionResources> resources_;
    BoostSession session_;
    GpuMonitor& monitor_;
    bool restored_ = false;
};
} // namespace fanctl
