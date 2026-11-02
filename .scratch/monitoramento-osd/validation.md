# Validação da implementação 0.5.0

Data: 2026-10-28.

## Compilação e testes

- `tools/test.ps1`: aprovado. Inclui regressões de BoostSession, ControlSession, fonte GPU, perfis automáticos, recuperação/início, histórico SQLite, isolamento do histórico e gráficos; inclui novos testes de monitoramento e OSD.
- `tools/build-installer.ps1`: aprovado com compilação C++17 `/W4 /WX /O2 /MT`. Instalador gerado: `dist/FanController-Setup-0.5.0-x64.exe`.
- Logs: `test-results.txt` e `build-results.txt` nesta pasta.
- Prévia forçada, manifesto asInvoker, em `build/preview/FanController.exe`: conferida visualmente com Computer Use. Painel com dados simulados e gráficos, aba OSD com controles legíveis e aba Controle com distinção boost/RPM. Corrigidos contraste dos checkboxes, fundo das abas e isolamento da janela/instância de prévia.

## REA e interfaces locais

Inventário inicial: `rea-investigation.md`. Inspeção adicional de exports do RTSSHooks64.dll pelo MCP REA: `rtss-hooks-exports.json`, evidência `ev_ab69015fa169406196fb3b6b3b6d523d279184dc32dcd0dfdee21c30d6bda4c0`. Diretório de exports completo, 35 entradas; isso não comprova execução de exports. A implementação usa o contrato de memória compartilhada e as tags de posição dos SDKs locais, sem chamar exports de injeção ou modificar perfis RTSS.

## Validação no computador e jogo

- Euro Truck Simulator 2 identificado pelo manifesto Steam 227300 e executável `D:\SteamLibrary\steamapps\common\Euro Truck Simulator 2\bin\win_x64\eurotrucks2.exe`.
- RTSS e jogo iniciados para teste. Nenhuma viagem foi iniciada; conferência feita no menu. Não foram aceitas opções de recursos online do jogo nem feitas mudanças em seu perfil.
- Sonda explícita `tests/osd_probe.cpp`, compilada por `tools/build-osd-probe.ps1`, publicou durante um minuto em slot próprio e o liberou ao encerrar. Não altera fans ou preferências persistidas. Logs: `live-osd-probe.jsonl` e `live-osd-thermal-probe.jsonl`.
- OSD observado visualmente no canto superior esquerdo do Euro Truck, com linhas CPU, GPU, Jogo e Fans. Leitura real de uso CPU/GPU, temperatura NVIDIA, FPS e tempo de quadro. Exemplo registrado: GPU 58 °C e 24%; jogo aproximadamente 240 FPS e 6,5 ms em uma amostra. Não são resultados de benchmark nem medidas de desempenho em viagem.
- Dados RTSS vinculados ao PID 23264 do jogo quando em primeiro plano. Ao alternar para outra janela, FPS e tempo de quadro passam a indisponíveis. Nunca são inventados valores para aplicativo sem entrada correspondente.
- Em repouso, NVML utilização retornou 999 (erro desconhecido) e os contadores Windows não tinham entradas do adaptador NVIDIA. Sob carga no jogo, NVML passou a responder. O comportamento de indisponibilidade foi preservado; o fallback Windows foi testado com dados simulados, sem comprovação de leitura NVIDIA real nesta máquina.
- A sonda de consulta Dell recebeu `0x80041003` (acesso negado) sem token de administrador. Por isso, temperatura CPU e RPM foram mostrados como indisponíveis no teste do OSD; sua passagem pelo cache foi validada com testes simulados. A versão normal mantém manifesto de administrador e recebe essas métricas do ciclo térmico existente.

## Limites e próxima conferência humana

O FanController instalado não foi substituído nem sua sessão de controle encerrada para testar a versão nova. É necessário instalar 0.5.0 com elevação, após encerrar pelo comando Sair para permitir a restauração, e conferir CPU provável/RPM no painel e OSD. Também faltam ensaio em viagem e conferência visual dos demais cantos e de escalas DPI diferentes; os testes de formatação verificam os quatro códigos de posição. O histórico existente não foi migrado ou apagado.
