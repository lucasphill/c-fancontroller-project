# Fonte de FPS e tempo de quadro sem RTSS

Pesquisa de viabilidade em fontes primárias, 2026-10-28. Não houve instalação, compilação, captura ETW ou instrumentação de jogos nesta pesquisa. A arquitetura continua pendente da entrevista.

## Conclusão

Há um candidato concreto: integrar a biblioteca de coleta/análise do PresentMon ao FanController e desenhar uma janela de OSD própria. A implementação oficial contém um alvo **estático** `pmon_present_data`, que gera `PresentData.lib`; portanto, a possibilidade de vinculação não depende apenas de inferência pelo nome. Isso não significa que ela seja um SDK estável pronto para inserir no build atual: depende de `pmon::common_utilities`, das opções de compilação do projeto e de `advapi32`/`tdh`. Compatibilidade, custo de coleta e dados reais no Euro Truck ainda precisam de prova local. [CMake oficial de PresentData](https://github.com/GameTechDev/PresentMon/blob/main/PresentData/CMakeLists.txt).

## Alternativas

1. **Biblioteca PresentData vinculada ao aplicativo ou componente próprio:** candidata recomendada para uma prova técnica sem RTSS e sem serviço persistente adicional. O repositório a descreve como analisador de eventos ETW. Isso permite uma rota sem carregar código no jogo; não comprova cobertura ou precisão nesta máquina. [Arquitetura oficial](https://github.com/GameTechDev/PresentMon/blob/main/README.md).
2. **Console PresentMon como componente interno gerenciado:** oferece filtro por PID, saída stdout e encerramento junto ao processo-alvo. É um executável adicional, mas não exige que o usuário o abra manualmente. Mais simples para comparar dados em um protótipo; envolve parsing e gerenciamento de processo. [Console oficial](https://github.com/GameTechDev/PresentMon/blob/main/README-ConsoleApplication.md).
3. **PresentMon API2:** a documentação oficial orienta clientes a carregar a DLL disponibilizada com o PresentMon Service. Essa API não representa uma DLL autônoma sem serviço; distribuir outra cópia da DLL pode romper compatibilidade. Não é a primeira recomendação para esta funcionalidade. [SDK/serviço](https://github.com/GameTechDev/PresentMon/blob/main/README-Service.md).

## Contrato dos dados e cuidados de integração

O consumidor expõe `mFilteredProcessIds`, `AddTrackedProcessForFiltering` e `RemoveTrackedProcessForFiltering`, além de `DequeuePresentEvents`. As apresentações são ordenadas por tempo dentro de cada swapchain, mas não necessariamente entre swapchains. O consumidor informa eventos perdidos e buffers podem transbordar se não forem drenados. A integração deve restringir o PID do jogo, escolher sua swapchain, atualizar a identidade após reinício e expirar valores; não pode contar apresentações da própria janela de OSD como FPS do jogo. [Interface oficial do consumidor](https://github.com/GameTechDev/PresentMon/blob/main/PresentData/PresentMonTraceConsumer.hpp).

É preciso definir a semântica: intervalo entre apresentações da aplicação, tempo efetivamente mostrado na tela e duração de trabalho GPU são medidas distintas. O CSV diferencia métricas CPU/GPU, `DisplayLatency` e `DisplayedTime`, e quadros não exibidos podem estar presentes. Não apresentar `MsGPUTime` como o tempo de quadro que o usuário espera. Para o painel simples, a candidata é FPS médio em janela recente e tempo entre apresentações da aplicação, explicitamente identificado; sua equivalência com o valor antigo RTSS não foi demonstrada. [Definições das métricas](https://github.com/GameTechDev/PresentMon/blob/main/README-ConsoleApplication.md).

## Execução e build

ETW ao vivo exige acesso adequado. O README documenta associação a Performance Log Users e limitações de identificação de processos sem elevação; o FanController normal já tem manifesto de administrador, mas essa condição precisa ser comprovada no teste real, sem alterar grupos do Windows nesta pesquisa. Há limitações documentadas para instrumentação OpenGL/Vulkan e métricas GPU sob Hardware-Accelerated GPU Scheduling. [Requisitos/limitações](https://github.com/GameTechDev/PresentMon/blob/main/README.md).

A documentação de build atual usa Windows x64, MSVC/Visual Studio 2022 ou 2026, SDK Windows, CMake >=4.2.2, Python >=3.10, Git e vcpkg. O caminho standalone console evita CEF, Node e instalador completo. O FanController atual usa scripts MSVC C++17; incorporar PresentData exigirá fixar uma revisão, examinar os requisitos efetivos de C++/runtime dessa revisão e decidir como integrar seu build. Não foi validada compatibilidade binária ou de compilação com os scripts atuais. [Build oficial](https://github.com/GameTechDev/PresentMon/blob/main/BUILDING.md).

## Distribuição

O projeto publica licença MIT e exige preservar avisos. Há avisos de terceiros separados; conferir os componentes efetivamente incorporados na revisão escolhida e entregar os avisos correspondentes. Nenhum binário do RTSS é necessário para essa alternativa. Isso descreve a licença publicada do candidato, não uma conclusão sobre permissões de reutilização do RTSS. [Licença](https://github.com/GameTechDev/PresentMon/blob/main/LICENSE.txt), [avisos de terceiros](https://github.com/GameTechDev/PresentMon/blob/main/THIRD_PARTY.txt).

## Questões ainda não respondidas

- Qual revisão será fixada e qual build mínimo passa com o toolchain local?
- O Euro Truck na API/modo gráfico em uso publica os eventos necessários e qual swapchain deve ser usada?
- Qual média e intervalo exibiremos como FPS/tempo de quadro?
- Quanto CPU/memória a captura consome no notebook e como o coletor encerra/libera apenas sua sessão ETW?
- Qual comportamento apresentar para permissão insuficiente, eventos perdidos, suspensão e reinício do jogo?

O REA continua útil para esclarecer comportamento do artefato RTSS quando houver uma pergunta concreta não respondida. A fonte aberta do PresentMon deve ser estudada pelas ferramentas normais de código; não é necessário repetir inventário ou descompilar fontes disponíveis.
