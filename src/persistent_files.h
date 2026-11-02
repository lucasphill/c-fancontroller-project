#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
namespace fanctl {
inline std::wstring dataPath(const wchar_t* name) {
    wchar_t executable[32768]{};
    const auto size=GetModuleFileNameW(nullptr,executable,32768);
    if (!size || size>=32768) throw std::runtime_error("Caminho de dados indisponivel.");
    return (std::filesystem::path(executable).parent_path()/name).wstring();
}
inline std::optional<std::string> readPersistent(const wchar_t* name) {
    const auto path=dataPath(name);
    if (GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES) {
        if (GetLastError()==ERROR_FILE_NOT_FOUND) return {};
        throw std::runtime_error("Nao foi possivel acessar o arquivo de configuracao.");
    }
    std::ifstream file{std::filesystem::path(path),std::ios::binary};
    if (!file) throw std::runtime_error("Nao foi possivel ler o arquivo de configuracao.");
    file.seekg(0,std::ios::end);
    if (file.tellg()<0 || file.tellg()>16384) throw std::runtime_error("Tamanho de configuracao invalido.");
    file.seekg(0);std::string text((std::istreambuf_iterator<char>(file)),{});
    if (file.bad()) throw std::runtime_error("Falha ao ler configuracao.");
    return text;
}
inline void writePersistent(const wchar_t* name,const std::string& text) {
    const auto destination=dataPath(name),temporary=destination+L".tmp";
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) throw std::runtime_error("Nao foi possivel salvar a configuracao.");
    DWORD written=0;
    const bool saved=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr) &&
        written==text.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!saved || !MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());throw std::runtime_error("Nao foi possivel concluir o salvamento.");
    }
}
} // namespace fanctl
