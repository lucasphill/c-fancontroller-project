#pragma once
#include "boost_session.h"
#include "gpu_temperature.h"
#include <charconv>

namespace fanctl {
enum class Command { Help, Status, Watch, Boost };
struct Options {
    Command command = Command::Help;
    GpuSource gpuSource = GpuSource::Dell;
    std::vector<Adjustment> adjustments;
};
inline std::uint32_t parseNumber(const std::string& text) {
    std::uint32_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size())
        throw std::runtime_error("Use numeros inteiros sem sinal.");
    return value;
}
inline Options parseOptions(const std::vector<std::string>& args) {
    Options options;
    if (args.empty() || (args.size() == 1 && (args[0] == "--help" || args[0] == "-h"))) return options;
    if (args[0] == "status") options.command = Command::Status;
    else if (args[0] == "watch") options.command = Command::Watch;
    else if (args[0] == "boost") options.command = Command::Boost;
    else throw std::runtime_error("Comando invalido. Consulte --help.");
    bool hasSource = false;
    for (std::size_t i = 1; i < args.size(); i += 2) {
        if (i + 1 >= args.size()) throw std::runtime_error("Opcao sem valor: " + args[i]);
        if (args[i] == "--gpu-source") {
            if (hasSource) throw std::runtime_error("Informe --gpu-source apenas uma vez.");
            hasSource = true;
            if (args[i + 1] == "dell") options.gpuSource = GpuSource::Dell;
            else if (args[i + 1] == "nvml") options.gpuSource = GpuSource::Nvml;
            else throw std::runtime_error("Fontes GPU permitidas: dell e nvml.");
        } else if (args[i] == "--fan" && options.command == Command::Boost) {
            const auto separator = args[i + 1].find('=');
            if (separator == std::string::npos) throw std::runtime_error("Use --fan ID=boost.");
            const auto id = parseNumber(args[i + 1].substr(0, separator));
            const auto boost = parseNumber(args[i + 1].substr(separator + 1));
            if ((id != 50 && id != 51) || boost > maxRequestedBoost)
                throw std::runtime_error("Fans permitidas: 50 e 51. Boost permitido: 0 a 100.");
            for (const auto& request : options.adjustments)
                if (request.id == id) throw std::runtime_error("Informe cada fan uma unica vez.");
            options.adjustments.push_back({id, boost});
        } else throw std::runtime_error("Opcao invalida para este comando: " + args[i]);
    }
    if (options.command == Command::Boost && options.adjustments.empty())
        throw std::runtime_error("Informe pelo menos uma --fan ID=boost.");
    return options;
}
} // namespace fanctl
