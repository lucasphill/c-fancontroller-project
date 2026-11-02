# Resultado do protótipo independente

Data: 2026-10-28. Experimento separado da versão instalada.

## Resultado observado

A faixa própria apareceu centralizada no topo do menu do Euro Truck Simulator 2, em uma única linha, com fundo escuro translúcido e texto pequeno. Houve leitura real de CPU provável, uso CPU, temperatura/uso NVIDIA, RPM das fans Dell, FPS e intervalo médio entre apresentações. RTSS não estava em execução durante a prova.

Exemplo de registro: CPU 67 °C; GPU 55 °C; fans 1.678/2.045 RPM; 240,30 FPS; 4,16 ms. Esses números são observações pontuais do menu, não resultados de benchmark ou de uma viagem. O tempo apresentado é o intervalo médio entre apresentações da aplicação em uma janela recente de aproximadamente um segundo; não é uma medição de duração de trabalho da GPU.

O teste sem elevação apresentou uso CPU/GPU e temperatura NVIDIA, mas ETW e Dell retornaram acesso negado. Após a confirmação de UAC pelo usuário, a execução elevada passou a obter os sensores Dell e os frames. Não foram modificados grupos, permissões, tarefas de login ou serviços.

A execução elevada foi encerrada automaticamente pelo limite de dez minutos; a ausência do processo PID 1124 foi conferida ao final. O protótipo não permaneceu coletando em segundo plano após a prova.

Uma perda de evento foi registrada durante a captura. O protótipo descarta o intervalo afetado e espera dados novos, em vez de invalidar todas as leituras futuras; os registros mostram leituras válidas com o contador acumulado de perdas igual a 1.

## Evidências

- `prototype-live.jsonl`: registros locais habilitados explicitamente com --record. Contém períodos sem elevação e elevados; distinguir disponibilidade por campos, não interpretar todos como uma única captura homogênea.
- `prototype-summary.json`: resumo pontual gerado durante a prova. Na amostra resumida, 517 registros, 205 com FPS, 336 com sensores Dell e zero registros que declarassem a faixa visível com o jogo fora de foco. A captura continuou depois desse resumo.
- Conferência visual via Computer Use: faixa no menu com todos os dados reais, incluindo aproximadamente 240 FPS/4,2 ms. Não foram carregadas viagens ou alterados perfis de jogo.
- `prototype-dependencies.txt`: imports estáticos do executável somente de DLLs do Windows. PresentData foi vinculada ao mesmo executável; não há dependência RTSS ou serviço PresentMon. NVML é carregada dinamicamente a partir do driver NVIDIA já instalado.
- `prototype-build.log`: compilação do código do protótipo com C++17 /W4 /WX; upstream original com /W3, sem alterar seus arquivos.
- `prototype-final-build.log`: recompilação final em build/overlay-prototype-verified após corrigir somente o texto de status ao ocultar. Pacote de revisão em build/FanController-Overlay-Prototype.zip, incluindo licença/avisos; hash em prototype-artifact-hash.json.
- `third_party/presentmon-prototype/REVISION.txt`: PresentMon v1.10.0, commit 2ce1158783e570539119f577d894252b395cadca. Licença e avisos acompanham o executável.

## Custo pontual

`prototype-cost.json`: em 112,75 segundos, o processo consumiu 2,64 segundos de CPU, aproximadamente 2,34% de um núcleo lógico, com working set de 46,43 MiB e memória privada de 23,05 MiB. A medição inclui painel, faixa, leitura de sensores e ETW. Não isola o custo de cada componente nem estabelece impacto no FPS ou uma média universal.

## Limites e próximos testes

- A visibilidade usa uma verificação periódica de 250 ms. Pode haver uma transição breve em Alt+Tab até o próximo ciclo; os registros posteriores confirmam ocultação. Antes da integração final, melhorar a reação a eventos de foco e validar o resultado visual.
- Estilos WS_EX_TRANSPARENT/WS_EX_NOACTIVATE, WM_NCHITTEST e WM_MOUSEACTIVATE evitam captura de interação. A geometria de entrada do automatizador no jogo não foi confiável para comprovar cliques em pontos exatos; uma conferência manual de clique/teclado continua indicada.
- A faixa funcionou no modo gráfico atual do jogo. Não foi estabelecido que esse modo fosse tela cheia exclusiva ou janela sem bordas; não inferir suporte universal a tela cheia exclusiva dessa observação. Alt+Enter não produziu uma mudança de modo visualmente comprovada. Testar janela e sem bordas explicitamente antes da integração final.
- Não houve ensaio em viagem, validação em vários monitores, escalas DPI variadas ou teste de múltiplas swapchains relevantes. O protótipo escolhe a swapchain com mais apresentações recentes; essa heurística exige revisão para uso geral.
- Nenhuma escrita de boost, perfil, clock, tensão ou limite de potência foi feita. O FanController instalado e seu banco de histórico foram preservados. O protótipo tem encerramento temporizado e libera somente a sessão ETW criada por ele.

## Decisão sustentada pela prova

É viável continuar pela arquitetura externa com medição independente, sem incorporar os binários RTSS. O protótipo não constitui ainda uma versão de produção. A integração e os testes restantes devem ser aprovados na continuação da entrevista.
