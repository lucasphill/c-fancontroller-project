#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wbemidl.h>
#include <wrl/client.h>
#include "boost_session.h"
#include "cli.h"
#include "nvml_temperature.h"
#include "windows_backend.h"

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
#include <charconv>

using Microsoft::WRL::ComPtr;

namespace {
using fanctl::ComRuntime;
using fanctl::DellReader;
using fanctl::BoostLock;
using fanctl::SessionJournal;
using fanctl::Snapshot;
using fanctl::readGpu;
using fanctl::isAdministrator;
HANDLE stopEvent = nullptr;
BOOL WINAPI consoleHandler(DWORD signal) {
    if (signal != CTRL_C_EVENT && signal != CTRL_BREAK_EVENT) return FALSE;
    SetEvent(stopEvent);
    return TRUE;
}

class StopSignal {
public:
    StopSignal() {
        stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stopEvent) throw std::runtime_error("Nao foi possivel criar sinal de parada.");
        if (!SetConsoleCtrlHandler(consoleHandler, TRUE)) {
            CloseHandle(stopEvent);
            stopEvent = nullptr;
            throw std::runtime_error("Nao foi possivel registrar Ctrl+C.");
        }
    }
    ~StopSignal() {
        SetConsoleCtrlHandler(consoleHandler, FALSE);
        // The event remains valid until process exit, including an in-flight handler.
    }
    StopSignal(const StopSignal&) = delete;
    StopSignal& operator=(const StopSignal&) = delete;
};

void print(const Snapshot& snapshot, const fanctl::GpuReading& gpu) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char timestamp[40]{};
    std::snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02u %02u:%02u:%02u",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    std::cout << timestamp << " | Perfil: " << snapshot.profile << '\n';
    for (const auto& fan : snapshot.fans)
        std::cout << "Fan " << fan.id << ": " << fan.rpm << " RPM | boost " << fan.boost << '\n';
    for (const auto& sensor : snapshot.sensors) {
        if (sensor.id == 6) continue; // The selected GPU source is rendered below, without implicit fallback.
        std::cout << "Sensor " << sensor.id << (sensor.id == 1 ? " (CPU provavel): " : ": ")
            << sensor.temperature << " C [Dell]\n";
    }
    std::cout << "GPU NVIDIA: ";
    if (gpu.temperature) std::cout << *gpu.temperature << " C";
    else std::cout << "indisponivel";
    if (gpu.source == fanctl::GpuSource::Nvml) std::cout << " [NVML]";
    else std::cout << " [Dell, sensor 6; sem validacao NVIDIA]";
    if (!gpu.error.empty()) std::cout << " - " << gpu.error;
    std::cout << '\n';
    std::cout << std::flush;
}

void help() {
    std::cout << "fanctl - Dell G15 5530\n"
        "  fanctl status   Leitura unica de RPM, temperaturas, boost e perfil\n"
        "  fanctl watch    Leituras a cada 2 segundos; Ctrl+C para sair\n"
        "  fanctl boost --fan 50=30 [--fan 51=30]\n"
        "    Boost 0 a 100. Mantem sessao aberta e restaura ao receber Ctrl+C.\n"
        "    Entra em Custom; restaura perfil e boosts anteriores ao sair.\n"
        "  --gpu-source dell|nvml   Opcao de status, watch e boost; padrao: dell\n"
        "    NVML e opcional e pode interferir no repouso da GPU.\n"
        "Execute os comandos em um terminal como administrador.\n"
        "Sensor 1: CPU provavel. Sensor 6: GPU. Boost nao e porcentagem de RPM.\n";
}

int runBoost(DellReader& reader, const std::vector<fanctl::Adjustment>& requests, fanctl::GpuMonitor& monitor) {
    StopSignal stop;
    fanctl::ControlSession session(reader, monitor, requests, std::make_unique<fanctl::WindowsSessionResources>());
    int result = 0;
    try {
        std::cout << "Estado anterior salvo. Entrando em Custom. Ctrl+C para restaurar perfil e boosts.\n";
        session.apply([] { return WaitForSingleObject(stopEvent, 0) != WAIT_TIMEOUT; });
        while (WaitForSingleObject(stopEvent, 0) == WAIT_TIMEOUT) {
            const auto frame = session.sample();
            print(frame.snapshot, frame.gpu);
            const auto wait = WaitForSingleObject(stopEvent, 2000);
            if (wait == WAIT_OBJECT_0) break;
            if (wait != WAIT_TIMEOUT) throw std::runtime_error("Falha na espera da sessao.");
        }
    } catch (const std::exception& error) {
        std::cerr << "Sessao interrompida: " << error.what() << '\n';
        result = 1;
    }
    const auto restoration = session.restore();
    for (const auto& message : restoration.messages) std::cout << message << '\n';
    if (!restoration.verified) {
        std::cerr << "RESTAURACAO NAO CONFIRMADA. Estado anterior preservado em fanctl-session.txt junto ao executavel.\n";
        return 3;
    }
    std::cout << "Perfil e boosts anteriores confirmados. Sessao encerrada.\n";
    return result;
}
} // namespace

int main(int argc, char** argv) {
    fanctl::Options options;
    try {
        options = fanctl::parseOptions(std::vector<std::string>(argv + 1, argv + argc));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
    if (options.command == fanctl::Command::Help) { help(); return 0; }
    try {
        if (!isAdministrator()) {
            std::cerr << "A interface Dell exige elevacao. Abra o terminal como administrador.\n";
            return 1;
        }
        ComRuntime runtime;
        DellReader reader;
        fanctl::GpuMonitor monitor(options.gpuSource, [] { return std::make_unique<fanctl::NvmlTemperature>(); });
        if (options.command == fanctl::Command::Boost) return runBoost(reader, options.adjustments, monitor);
        if (options.command == fanctl::Command::Status) {
            const auto snapshot = reader.read();
            const auto gpu = readGpu(snapshot, monitor);
            print(snapshot, gpu);
            return gpu.temperature ? 0 : 1;
        }
        StopSignal stop;
        std::cout << "Somente leitura. Ctrl+C para sair.\n";
        bool hadUnavailableGpu = false;
        while (WaitForSingleObject(stopEvent, 0) == WAIT_TIMEOUT) {
            const auto snapshot = reader.read();
            const auto gpu = readGpu(snapshot, monitor);
            print(snapshot, gpu);
            hadUnavailableGpu = hadUnavailableGpu || !gpu.temperature;
            const auto wait = WaitForSingleObject(stopEvent, 2000);
            if (wait == WAIT_OBJECT_0) break;
            if (wait != WAIT_TIMEOUT) throw std::runtime_error("Falha ao aguardar proxima leitura.");
        }
        std::cout << "Monitoramento encerrado.\n";
        return hadUnavailableGpu ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "Erro: " << error.what() << '\n';
        return 1;
    }
}

