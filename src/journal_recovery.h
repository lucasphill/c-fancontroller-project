#pragma once
#include "boost_session.h"
#include <sstream>
namespace fanctl {
inline Snapshot decodeJournal(const std::string& text) {
    std::istringstream input(text);std::string line;
    if (!std::getline(input,line) || line!="fanctl session v1") throw std::runtime_error("Registro de sessao invalido.");
    Snapshot state{};bool profile=false,first=false,second=false;
    while (std::getline(input,line)) {
        if (line.empty()) continue;
        if (line.rfind("requested ",0)==0) continue;
        if (line.rfind("profile=",0)==0 && !profile) {
            const auto value=line.substr(8);std::size_t used=0;const auto n=std::stoul(value,&used);
            if (used!=value.size() || (n!=0 && n!=160)) throw std::runtime_error("Perfil do registro invalido.");
            state.profile=static_cast<std::uint32_t>(n);profile=true;
        } else if (line.rfind("original 50=",0)==0 || line.rfind("original 51=",0)==0) {
            const auto id=line[10]=='0' ? 50U : 51U;
            auto& seen=id==50 ? first : second;
            const auto value=line.substr(12);std::size_t used=0;const auto n=std::stoul(value,&used);
            if (seen || used!=value.size() || n>255) throw std::runtime_error("Boost do registro invalido.");
            state.fans.push_back({id,0,static_cast<std::uint32_t>(n)});seen=true;
        } else throw std::runtime_error("Linha de registro invalida.");
    }
    if (!profile || !first || !second) throw std::runtime_error("Registro de sessao incompleto.");
    return state;
}
// Caller holds the global boost lease and retains the journal on any failure.
inline void recoverJournal(ThermalBackend& backend,const Snapshot& original) {
    for (int attempt=0;attempt<2;++attempt) {
        try {
            backend.setProfile(original.profile);
            if (backend.readProfile()!=original.profile) continue;
            for (const auto& fan : original.fans) backend.setBoost(fan.id,fan.boost);
            if (backend.readProfile()==original.profile && backend.readBoost(50)==findFan(original,50).boost &&
                backend.readBoost(51)==findFan(original,51).boost) return;
        } catch (const std::exception&) {if (attempt==1) throw;}
    }
    throw std::runtime_error("Recuperacao do estado anterior nao confirmada.");
}
} // namespace fanctl
