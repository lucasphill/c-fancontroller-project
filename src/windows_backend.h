#pragma once
#include <windows.h>
#include <wbemidl.h>
#include <wrl/client.h>
#include <cstdio>
#include <limits>
#include "boost_session.h"
#include "gpu_temperature.h"
#include "control_session.h"

namespace fanctl {
using Microsoft::WRL::ComPtr;
inline void check(HRESULT result, const char* operation) {
    if (SUCCEEDED(result)) return;
    char code[16]{};
    std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(result));
    throw std::runtime_error(std::string(operation) + ": " + code);
}

class Bstr {
public:
    explicit Bstr(const wchar_t* text) : value_(SysAllocString(text)) {
        if (!value_) throw std::bad_alloc();
    }
    ~Bstr() { SysFreeString(value_); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;
    operator BSTR() const { return value_; }
private:
    BSTR value_;
};

class Variant {
public:
    Variant() { VariantInit(&value); }
    ~Variant() { VariantClear(&value); }
    Variant(const Variant&) = delete;
    Variant& operator=(const Variant&) = delete;
    VARIANT value;
};

class ComRuntime {
public:
    ComRuntime() { check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Inicializar COM"); }
    ~ComRuntime() { CoUninitialize(); }
    ComRuntime(const ComRuntime&) = delete;
    ComRuntime& operator=(const ComRuntime&) = delete;
};

inline bool isAdministrator() {
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID administrators = nullptr;
    if (!AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &administrators)) {
        throw std::runtime_error("Nao foi possivel verificar os privilegios.");
    }
    BOOL member = FALSE;
    const BOOL success = CheckTokenMembership(nullptr, administrators, &member);
    FreeSid(administrators);
    if (!success) throw std::runtime_error("Nao foi possivel verificar os privilegios.");
    return member != FALSE;
}

enum class Query : std::uint32_t {
    Description = 2, Resource = 3, Temperature = 4, Rpm = 5, Profile = 11, Boost = 12
};

using fanctl::Snapshot;

class DellReader : public fanctl::ThermalBackend {
public:
    DellReader() {
        check(CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
            RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr), "Seguranca COM");
        ComPtr<IWbemLocator> locator;
        check(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(locator.GetAddressOf())), "Criar localizador WMI");
        Bstr space(L"ROOT\\WMI");
        check(locator->ConnectServer(space, nullptr, nullptr, nullptr, WBEM_FLAG_CONNECT_USE_MAX_WAIT,
            nullptr, nullptr, services_.GetAddressOf()), "Conectar WMI");
        check(CoSetProxyBlanket(services_.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
            nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr,
            EOAC_NONE), "Autenticacao WMI");

        ComPtr<IEnumWbemClassObject> enumeration;
        Bstr language(L"WQL");
        Bstr query(L"SELECT * FROM AWCCWmiMethodFunction");
        check(services_->ExecQuery(language, query,
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
            enumeration.GetAddressOf()), "Consultar interface Dell");
        ComPtr<IWbemClassObject> instance;
        ULONG count = 0;
        check(enumeration->Next(5000, 1, instance.GetAddressOf(), &count), "Ler instancia Dell");
        if (count != 1) throw std::runtime_error("Interface Dell ausente ou consulta expirou.");
        Variant path;
        check(instance->Get(L"__PATH", 0, &path.value, nullptr, nullptr), "Ler caminho WMI");
        if (path.value.vt != VT_BSTR || !path.value.bstrVal)
            throw std::runtime_error("Caminho WMI invalido.");
        path_ = path.value.bstrVal;
        ComPtr<IWbemClassObject> second;
        count = 0;
        check(enumeration->Next(5000, 1, second.GetAddressOf(), &count), "Verificar instancia unica");
        if (count != 0) throw std::runtime_error("Mais de uma instancia Dell encontrada.");

        Bstr className(L"AWCCWmiMethodFunction");
        ComPtr<IWbemClassObject> definition;
        check(services_->GetObject(className, 0, nullptr, definition.GetAddressOf(), nullptr), "Ler classe Dell");
        ComPtr<IWbemClassObject> inputDefinition;
        check(definition->GetMethod(L"Thermal_Information", 0, inputDefinition.GetAddressOf(), nullptr),
            "Ler assinatura de consulta termica");
        check(inputDefinition->SpawnInstance(0, arguments_.GetAddressOf()), "Preparar consulta termica");
    }

    Snapshot read() override {
        ++readAttempt_;partial_={0,{},{}};
        const auto description = query(Query::Description);
        const auto fanCount = description & 0xff;
        const auto sensorCount = (description >> 8) & 0xff;
        if (fanCount != 2 || sensorCount > 32)
            throw std::runtime_error("Topologia inesperada para este Dell; leitura interrompida.");
        partial_.profile=query(Query::Profile);
        auto& result=partial_;
        for (std::uint32_t index = 0; index < fanCount; ++index) {
            const auto id = query(Query::Resource, index);
            if ((id != 50 && id != 51) ||
                (!result.fans.empty() && result.fans.front().id == id))
                throw std::runtime_error("ID de fan inesperado ou duplicado.");
            result.fans.push_back({id, UINT32_MAX, UINT32_MAX});
            result.fans.back().rpm=query(Query::Rpm,id);
            result.fans.back().boost=query(Query::Boost,id);
        }
        for (std::uint32_t index = fanCount; index < fanCount + sensorCount; ++index) {
            const auto resource = query(Query::Resource, index);
            // Resource type 0x100 marks a temperature sensor. Queries use its low byte.
            if (resource <= 0x100 || resource >= 0x1a0)
                throw std::runtime_error("Tipo de recurso de temperatura inesperado.");
            const auto id = resource & 0xff;
            result.sensors.push_back({id, query(Query::Temperature, id)});
        }
        return result;
    }

    std::uint64_t readAttempt() const {return readAttempt_;}
    Snapshot partialSnapshot() const {return partial_;}

    std::uint32_t readProfile() override { return query(Query::Profile); }
    std::uint32_t readBoost(std::uint32_t id) override { return query(Query::Boost, id); }
    void setProfile(std::uint32_t profile) override {
        if (profile != 0 && profile != 160) throw std::runtime_error("Perfil nao validado.");
        if (invoke(L"Thermal_Control", (profile << 8) | 1) != 0)
            throw std::runtime_error("Firmware rejeitou troca de perfil.");
    }
    void setBoost(std::uint32_t id, std::uint32_t boost) override {
        if ((id != 50 && id != 51) || boost > 255)
            throw std::runtime_error("Ajuste invalido.");
        const auto result = invoke(L"Thermal_Control", (boost << 16) | (id << 8) | 2);
        if (result != 0) throw std::runtime_error("Firmware rejeitou boost da fan " + std::to_string(id));
    }

private:
    Snapshot partial_{0,{},{}};
    std::uint64_t readAttempt_=0;
    std::uint32_t query(Query operation, std::uint32_t id = 0) {
        if (id > 255) throw std::runtime_error("ID de consulta invalido.");
        return invoke(L"Thermal_Information", (id << 8) | static_cast<std::uint32_t>(operation));
    }

    std::uint32_t invoke(const wchar_t* methodName, std::uint32_t argument) {
        // Both methods use the same uint32 arg2 signature, checked by WMI.
        Variant input;
        input.value.vt = VT_I4;
        input.value.lVal = static_cast<LONG>(argument);
        check(arguments_->Put(L"arg2", 0, &input.value, 0), "Preparar argumento WMI");
        Bstr path(path_.c_str());
        Bstr method(methodName);
        ComPtr<IWbemCallResult> call;
        check(services_->ExecMethod(path, method, WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
            arguments_.Get(), nullptr, call.GetAddressOf()), "Iniciar leitura termica");
        ComPtr<IWbemClassObject> response;
        check(call->GetResultObject(5000, response.GetAddressOf()), "Obter leitura termica");
        if (!response) throw std::runtime_error("Resposta WMI vazia.");
        Variant output;
        check(response->Get(L"argr", 0, &output.value, nullptr, nullptr), "Ler retorno termico");
        std::uint32_t value;
        if (output.value.vt == VT_I4) value = static_cast<std::uint32_t>(output.value.lVal);
        else if (output.value.vt == VT_UI4) value = output.value.ulVal;
        else throw std::runtime_error("Tipo de retorno WMI inesperado.");
        if (value == std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("Operacao termica indisponivel; argumento " + std::to_string(argument));
        return value;
    }
    ComPtr<IWbemServices> services_;
    ComPtr<IWbemClassObject> arguments_;
    std::wstring path_;
};

class BoostLock {
public:
    BoostLock() {
        handle_ = CreateMutexW(nullptr, FALSE, L"Global\\FanControllerBoost");
        if (!handle_) throw std::runtime_error("Nao foi possivel criar trava de controle.");
        const auto wait = WaitForSingleObject(handle_, 0);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
            CloseHandle(handle_);
            handle_ = nullptr;
            throw std::runtime_error("Outra sessao de boost esta ativa ou a trava esta indisponivel.");
        }
    }
    ~BoostLock() { ReleaseMutex(handle_); CloseHandle(handle_); }
    BoostLock(const BoostLock&) = delete;
    BoostLock& operator=(const BoostLock&) = delete;
private:
    HANDLE handle_ = nullptr;
};

class SessionJournal {
public:
    SessionJournal(const Snapshot& state, const std::vector<fanctl::Adjustment>& requested) {
        std::vector<wchar_t> executable(32768);
        const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!length || length >= executable.size()) throw std::runtime_error("Caminho do executavel indisponivel.");
        path_ = std::wstring(executable.data(), length);
        const auto slash = path_.find_last_of(L"\\/");
        if (slash == std::wstring::npos) throw std::runtime_error("Diretorio do executavel indisponivel.");
        path_.resize(slash + 1);
        path_ += L"fanctl-session.txt";
        HANDLE file = CreateFileW(path_.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("Registro anterior existe ou nao pode ser criado. Confira o estado salvo antes de novo boost.");
        }
        std::string content = "fanctl session v1\nprofile=" + std::to_string(state.profile) + "\n";
        for (const auto& fan : state.fans)
            content += "original " + std::to_string(fan.id) + "=" + std::to_string(fan.boost) + "\n";
        for (const auto& request : requested)
            content += "requested " + std::to_string(request.id) + "=" + std::to_string(request.boost) + "\n";
        DWORD written = 0;
        const bool saved = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr)
            && written == content.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if (!saved) throw std::runtime_error("Nao foi possivel persistir o estado anterior. Nenhum ajuste enviado.");
    }
    void clear() {
        if (!DeleteFileW(path_.c_str())) throw std::runtime_error("Estado restaurado, mas nao foi possivel remover o registro da sessao.");
    }
private:
    std::wstring path_;
};

class WindowsSessionResources final : public SessionResources {
public:
    void save(const Snapshot& original, const std::vector<Adjustment>& requests) override {
        journal_ = std::make_unique<SessionJournal>(original, requests);
    }
    void clear() override { journal_->clear(); }
private:
    BoostLock lock_; // Acquired before the caller snapshots the original state.
    std::unique_ptr<SessionJournal> journal_;
};

inline fanctl::GpuReading readGpu(const Snapshot& snapshot, fanctl::GpuMonitor& monitor) {
    std::optional<int> dellTemperature;
    for (const auto& sensor : snapshot.sensors)
        if (sensor.id == 6 && sensor.temperature <= 125) dellTemperature = static_cast<int>(sensor.temperature);
    return monitor.read(dellTemperature);
}


} // namespace fanctl

