# FanController — análise em andamento

## Decisões confirmadas

- Uso exclusivo neste Dell G15 5530.
- Interface inicial em terminal e baixo consumo de recursos.
- Objetivo: melhorar a circulação de ar e reduzir a temperatura média; ruído secundário.
- Buscar controle individual das fans e, futuramente, curvas por temperatura.
- Perfis térmicos são alternativa parcial, não atendimento integral do objetivo.
- Execução como administrador é aceitável se necessária.
- Buscar independência do Alienware Command Center instalado.
- Se for necessário instalar driver ou mudar configurações de segurança do Windows, avaliar esse custo antes de avançar.
- Pendências e documentação ficam em arquivos locais.
- Entrevista com uma pergunta por vez.
- Avaliar refrigeração comparando temperaturas de CPU e GPU sob a mesma carga, acompanhando também o desempenho; definir meta numérica após medir o comportamento atual.
- Controle por boost individual atende ao objetivo, sem exigir RPM fixo, desde que o firmware mantenha a possibilidade de elevar a ventilação quando necessário. Essa capacidade precisa ser validada nesta máquina.
- Primeira versão: boost fixo por fan aplicado pelo terminal. Curvas por temperatura ficam para etapa posterior à validação do controle e das medições.
- Inicialmente, a sessão de controle permanece aberta no terminal e tenta restaurar a configuração anterior ao receber Ctrl+C. Decisão provisória; não há garantia de restauração após encerramento forçado, travamento ou reinicialização.
- Condição de uso inicial: nenhum outro programa deve ajustar as fans durante a sessão. O programa não encerrará serviços ou aplicativos automaticamente.
- Linguagem da primeira versão: C++, pela integração direta com COM/WMI no Windows e pelas referências existentes. A escolha não implica garantia de menor consumo que Rust.
- Em falha parcial ao ajustar as fans, interromper a operação, tentar restaurar os ajustes anteriores e informar por fan o que foi aplicado e o resultado da restauração.
- Instalacao das ferramentas C++ Microsoft autorizada se ausentes: MSVC e Windows SDK, sem IDE completa.
- Comandos iniciais aprovados:
  - `fanctl status`: leitura unica de RPM, temperaturas, boost e perfil atual; encerra.
  - `fanctl watch`: atualiza as leituras a cada 2 segundos ate Ctrl+C.
  - `fanctl boost --fan 50=30 --fan 51=30`: sintaxe para boost por ID, mantendo a sessao aberta e tentando restaurar os valores anteriores em Ctrl+C. O numero 30 e ilustrativo; intervalo e efeito precisam de validacao.
- Antes de qualquer ajuste, exigir leitura bem-sucedida do perfil e dos boosts anteriores de todas as fans envolvidas; caso contrario, recusar a operacao sem escrever.
- Se as leituras falharem durante uma sessao de boost, tentar restaurar o estado anterior e encerrar, informando o resultado da restauracao.
- Etapa de implementacao atual autorizada: implementar e validar somente `status` e `watch`; `boost` fica para a etapa seguinte.
- Usuario validou funcionamento de status/watch. Teste temporario de escrita de boost, uma fan por vez com restauracao ao final, autorizado e executado.
- Implementacao do comando boost em C++, com restauracao e tratamento de falhas definidos, autorizada pelo usuario e concluida.
- Usuario relatou ausencia de efeito perceptivel. Comparacao temporaria autorizada
  entre equilibrado 160 e Custom 0 com boost 30 mostrou RPM maiores em Custom.
  Perfil 160 e boosts 0/0 restaurados; evidencias em docs/profile-comparison.md.
- Usuario autorizou a correcao do comando para entrar automaticamente em Custom
  e restaurar perfil e boosts anteriores ao sair. Implementado e validado;
  evidencias em docs/validation-custom-cli.md.

## Questões em aberto

- NVML direta testada em sonda separada: API atual funcionou em 15/15 amostras,
  diferenca maxima de 1 grau frente ao nvidia-smi nas seis comparacoes por tempo.
  Memoria da sonda ~24 MiB; somente temperatura teve baixo custo de CPU/latencia.
  Possivel impacto no repouso da GPU e gasto adicional ainda nao quantificado.
  Usuario autorizou fonte opcional sob demanda, desligada por padrao.
  Integrada em status/watch/boost com --gpu-source nvml; fonte Dell segue padrao.
  Erros NVML nao fazem fallback. Falha em boost recusa ou restaura os ajustes.
  Testes simulados e hardware com Ctrl+C passaram; perfil160 e boosts0/0 confirmados.
  Detalhes em docs/validation-nvml-integration.md.

- Investigacao de sensores concluida com limites: firmware liga fan50 ao sensor1,
  e fan51 ao sensor6. Sensor6 identificado como GPU por comparacao NVIDIA;
  sensor1 e provavel CPU, ainda sem referencia de temperatura independente.
  Leituras GPU isoladas de 30 graus nao confirmadas. Detalhes em docs/sensor-investigation.md.
- Definir politica de validade/fallback da temperatura GPU antes de implementar curvas.

- Usuario solicitou poder usar 100%; faixa de boost ampliada para 0–100.
  Pontos 0, 30, 50, 75 e 100 medidos no hardware em Custom; todos estabilizaram.
  Boost 100 resultou em aproximadamente 5.000 RPM nas duas fans nesta execucao.
  Dados e limites em docs/fan-calibration.md; nao estabelecem maximo fisico absoluto.

- Meta numérica de redução de temperatura e protocolo de medição do comportamento atual.
- Capacidades reais da interface térmica nesta máquina.
- Repetir a calibracao sob outras cargas apenas se necessario para projetar as curvas.
- Validacao pratica das politicas aprovadas de falhas e restauracao na etapa de controle.
- Limites de consumo de longo prazo; comandos iniciais definidos.

## Evidências preliminares

Estas fontes orientam a investigação; não comprovam funcionamento nesta máquina.

- Dell documenta offset de fan no G15 5530: https://www.dell.com/support/kbdoc/en-us/000221917/fan-speed-offset-configuration-on-performance-page-in-alienware-command-center
- Projeto tcc-g15 lista suporte reportado ao 5530: https://github.com/AlexIII/tcc-g15
- Interface WMI investigada pelo projeto: https://github.com/AlexIII/tcc-g15/blob/master/WMI-AWCC-doc.md
- Referência do kernel: https://docs.kernel.org/wmi/devices/alienware-wmi.html

## Estado

GUI inicial concluida em build/FanController.exe, Win32, sem console, com
bandeja e inicio em monitoramento. X/minimizar preserva processo e controle;
Restaurar/Sair tentam voltar ao estado anterior. Falha de restauracao mantem
janela e sessao para nova tentativa. CLI e GUI compartilham backend/controle.
Usuario confirmou fluxo X/bandeja/Restaurar. Testes passaram; detalhes em docs/gui.md.
Sem inicializacao automatica, preferencias salvas, servico ou curvas nesta etapa.

Etapa de leitura implementada em C++: status e watch, com COM/WMI direto.
Compilacao, leituras reais elevadas e encerramento por Ctrl+C validados.
Ferramentas MSVC e Windows SDK instaladas com sucesso, sem reiniciar.
Evidencias e limites: docs/validation-readonly.md.
Boost implementado no codigo: faixa ampliada 0–100, entrada automatica em Custom,
perfis de origem aceitos 160 ou 0, restauracao de perfil antes dos boosts, registro
persistente do estado anterior e trava de sessao unica. Restauracao por Ctrl+C
validada no hardware, com consulta WMI independente, e cenarios de falhas simulados.
Detalhes atuais em docs/validation-custom-cli.md. O teste terminou com perfil 160 e boost 0
nas duas fans, iguais ao estado inicial; registro de sessao removido.

Atualizacao 0–100: 18 cenarios simulados passaram. Binario atualizado compilado
inicialmente como build/fanctl-next.exe. Apos o usuario confirmar Ctrl+C e
restauracao, build/fanctl.exe foi atualizado; ajuda e igualdade dos hashes conferidas.
Nao foi enviado boost 100 ao hardware nesta atualizacao.

Calibracao posterior autorizada: script tools/calibrate-fans.ps1 executou os
cinco pontos em Custom com 15 amostras por ponto. Medias das cinco amostras finais:
fan 50 = 2017 / 2899 / 3483,6 / 4261,6 / 5006,2 RPM;
fan 51 = 2303,4 / 3111,4 / 3654 / 4331,8 / 4998,8 RPM.
Ao final, perfil 160 e boosts 0/0 restaurados e confirmados, sem erros.
Ganho termico sob carga padronizada e identificacao dos sensores continuam pendentes.

## Prova de leitura — 2026-10-24

- Classe AWCCWmiMethodFunction presente em root/WMI.
- Consulta da instancia e dos valores funcionou com elevacao UAC; processo comum recebeu acesso negado.
- Sonda local: tools/probe-dell-wmi.ps1; somente operacoes permitidas de Thermal_Information, sem Thermal_Control.
- Resultado persistido em probe-result.json: duas fans (50 e 51), boost atual 0 em ambas; RPM legivel.
- Dois recursos de temperatura, 0x101 e 0x106: marcador de tipo 0x100 e IDs de consulta 1 e 6. Codificacao conferida na descoberta do AlienFan SDK: https://github.com/T-Troll/alienfx-tools/blob/master/alienfan-tools/alienfan-SDK/alienfan-SDK.cpp
- Ultima amostra: fan 50 a 1693 RPM, fan 51 a 1903 RPM; sensores 1 e 6 a 73 e 57 graus Celsius. Amostra pontual, sem carga padronizada; nao constitui baseline nem medida de ganho.
- Perfil atual retornado: 160 (0xA0). Nomes CPU/GPU das fans e sensores ainda nao foram validados.
- Nenhum ajuste de boost ou perfil foi enviado. Escrita, efeito na refrigeracao e restauracao continuam sem validacao.
- Nao foram encontrados cl, clang++, g++, cmake ou ninja no PATH, nem MSVC pelo vswhere ou compiladores nas localizacoes comuns verificadas. Isso nao prova ausencia em todos os discos.
- Verificacao: script analisado pelo parser PowerShell e executado na maquina; leituras registradas em JSON. Nenhum driver instalado e nenhuma configuracao de seguranca alterada.

## Perfis automaticos — 25/10/2026

Decisoes aprovadas e implementacao descritas em automatic-profiles.md e
../../docs/automatic-profiles.md. GUI com seletor Manual/Normal/Performance,
aba Curvas, persistencia local e restauracao preservando o estado original.
Compilacoes GUI/CLI e tools/test.ps1 passaram, incluindo 11 cenarios de sessao
compartilhada e testes de curvas/histerese/RPM/configuracao. Validacao da nova
interface e dos perfis no hardware pendente: issues/01-validar-perfis-automaticos.md.
