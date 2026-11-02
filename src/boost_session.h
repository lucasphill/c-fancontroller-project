#pragma once
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fanctl {
inline constexpr std::uint32_t maxRequestedBoost = 100;
struct Fan { std::uint32_t id; std::uint32_t rpm; std::uint32_t boost; };
struct Sensor { std::uint32_t id; std::uint32_t temperature; };
struct Snapshot {
    std::uint32_t profile;
    std::vector<Fan> fans;
    std::vector<Sensor> sensors;
};
struct Adjustment { std::uint32_t id; std::uint32_t boost; };
struct RestoreResult { bool verified; std::vector<std::string> messages; };

class ThermalBackend {
public:
    virtual ~ThermalBackend() = default;
    virtual Snapshot read() = 0;
    virtual std::uint32_t readProfile() = 0;
    virtual std::uint32_t readBoost(std::uint32_t id) = 0;
    virtual void setProfile(std::uint32_t profile) = 0;
    virtual void setBoost(std::uint32_t id, std::uint32_t boost) = 0;
};

inline const Fan& findFan(const Snapshot& state, std::uint32_t id) {
    for (const auto& fan : state.fans) if (fan.id == id) return fan;
    throw std::runtime_error("Fan ausente: " + std::to_string(id));
}

// Owns rollback policy. The caller persists original() before calling apply().
class BoostSession {
public:
    BoostSession(ThermalBackend& backend, std::vector<Adjustment> requested)
        : backend_(backend), requested_(std::move(requested)), original_(backend.read()) {
        if (requested_.empty()) throw std::runtime_error("Nenhuma fan solicitada.");
        if (original_.profile != 0 && original_.profile != 160)
            throw std::runtime_error("Perfil original ainda nao validado; use Custom ou equilibrado.");
        if (original_.fans.size() != 2 || findFan(original_, 50).boost > 255 ||
            findFan(original_, 51).boost > 255)
            throw std::runtime_error("Estado anterior das fans nao restauravel.");
        for (std::size_t i = 0; i < requested_.size(); ++i) {
            const auto& request = requested_[i];
            if ((request.id != 50 && request.id != 51) || request.boost > maxRequestedBoost)
                throw std::runtime_error("Fan ou boost fora do intervalo permitido.");
            if (findFan(original_, request.id).boost > 255)
                throw std::runtime_error("Boost anterior fora do intervalo restauravel.");
            for (std::size_t j = 0; j < i; ++j)
                if (requested_[j].id == request.id) throw std::runtime_error("Fan duplicada.");
        }
    }
    const Snapshot& original() const { return original_; }

    void apply(const std::function<bool()>& stopping) {
        verifyOriginal();
        if (stopping()) return;
        const bool switchProfile = original_.profile != 0;
        if (switchProfile) {
            touched_ = true;
            backend_.setProfile(0);
            if (backend_.readProfile() != 0) throw std::runtime_error("Perfil Custom nao confirmado.");
        }
        // Changing profile can reset both fans, including an omitted one.
        for (const auto& previous : original_.fans) {
            if (stopping()) return;
            auto target = previous.boost;
            for (const auto& request : requested_) if (request.id == previous.id) target = request.boost;
            if (!switchProfile && target == previous.boost) continue;
            touched_ = true; // A throwing setter may still have changed firmware.
            backend_.setBoost(previous.id, target);
            if (backend_.readBoost(previous.id) != target)
                throw std::runtime_error("Boost nao confirmado: fan " + std::to_string(previous.id));
        }
    }

    Snapshot sample() {
        auto state = backend_.read();
        if (state.profile != 0) throw std::runtime_error("Perfil alterado durante a sessao.");
        for (const auto& previous : original_.fans) {
            auto expected = previous.boost;
            for (const auto& request : requested_) if (request.id == previous.id) expected = request.boost;
            if (findFan(state, previous.id).boost != expected)
                throw std::runtime_error("Boost alterado durante a sessao: fan " + std::to_string(previous.id));
        }
        return state;
    }

    void update(const std::vector<Adjustment>& requests, const std::function<bool()>& stopping) {
        if (requests.size()!=2 || requests[0].id!=50 || requests[1].id!=51 ||
            requests[0].boost<20 || requests[0].boost>100 || requests[1].boost<20 || requests[1].boost>100)
            throw std::runtime_error("Ajustes automaticos invalidos.");
        sample(); // Detect external changes before writing another target.
        for (const auto& request : requests) {
            if (stopping()) return;
            if (backend_.readBoost(request.id)==request.boost) continue;
            touched_=true;
            backend_.setBoost(request.id,request.boost);
            if (backend_.readBoost(request.id)!=request.boost)
                throw std::runtime_error("Boost automatico nao confirmado.");
        }
        requested_=requests;
    }

    RestoreResult restore() {
        RestoreResult result{true, {}};
        if (touched_) {
            bool profileVerified = false;
            for (int retry = 0; retry < 2 && !profileVerified; ++retry) {
                try { backend_.setProfile(original_.profile); }
                catch (const std::exception& error) {
                    result.messages.push_back(std::string("Restaurar perfil: ") + error.what());
                }
                try { profileVerified = backend_.readProfile() == original_.profile; }
                catch (const std::exception& error) {
                    result.messages.push_back(std::string("Ler perfil restaurado: ") + error.what());
                }
            }
            result.messages.push_back(profileVerified ? "Perfil anterior confirmado." : "RESTAURACAO DO PERFIL NAO CONFIRMADA.");
            result.verified = profileVerified;
        }
        // Restore profile first, then both boosts: profile writes can reset them.
        for (const auto& fan : original_.fans) {
            if (!touched_) break;
            const auto id = fan.id;
            const auto previous = fan.boost;
            bool verified = false;
            for (int retry = 0; retry < 2 && !verified; ++retry) {
                try { backend_.setBoost(id, previous); }
                catch (const std::exception& error) {
                    result.messages.push_back("Fan " + std::to_string(id) + ": " + error.what());
                }
                try { verified = backend_.readBoost(id) == previous; }
                catch (const std::exception& error) {
                    result.messages.push_back("Leitura de restauracao fan " + std::to_string(id) + ": " + error.what());
                }
            }
            result.messages.push_back("Fan " + std::to_string(id) + (verified ? ": boost anterior confirmado." : ": RESTAURACAO NAO CONFIRMADA."));
            result.verified = result.verified && verified;
        }
        // Confirm the full original state, even when no write was attempted.
        try {
            if (backend_.readProfile() != original_.profile) result.verified = false;
            for (const auto& fan : original_.fans)
                if (backend_.readBoost(fan.id) != fan.boost) result.verified = false;
        } catch (const std::exception& error) {
            result.messages.push_back(std::string("Verificacao final: ") + error.what());
            result.verified = false;
        }
        return result;
    }

private:
    void verifyOriginal() {
        if (backend_.readProfile() != original_.profile) throw std::runtime_error("Perfil alterado antes do ajuste.");
        for (const auto& fan : original_.fans)
            if (backend_.readBoost(fan.id) != fan.boost) throw std::runtime_error("Boost alterado antes do ajuste.");
    }
    ThermalBackend& backend_;
    std::vector<Adjustment> requested_;
    Snapshot original_;
    bool touched_ = false;
};
} // namespace fanctl
