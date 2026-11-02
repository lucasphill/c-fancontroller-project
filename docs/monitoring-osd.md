# Monitoramento e exibição na tela

O painel **Monitor** acompanha temperaturas, uso de CPU/GPU, RPM das duas fans e dados do jogo. Os gráficos mostram até 120 atualizações recentes; são dados ao vivo e não modificam o histórico SQLite, sua coleta a cada cinco minutos nem sua retenção de 90 dias.

## Faixa independente (0.6.0)

Na aba **OSD**, escolha as métricas e **Mostrar métricas no jogo**. A mesma alternância está no menu da bandeja. A faixa ocupa uma única linha, centralizada no topo da área do Euro Truck Simulator 2, com texto pequeno e fundo escuro translúcido. Na primeira execução começa desligada, com todas as métricas selecionadas. As preferências ficam em `%LOCALAPPDATA%\FanController\osd-settings.txt`, preservadas nas atualizações.

RTSS, MSI Afterburner e serviço PresentMon não são necessários. O renderizador e o coletor de frames fazem parte do mesmo executável. A versão 0.6.0 lê as preferências anteriores, preservando ativação e seleção de métricas; os quatro cantos da versão RTSS dão lugar ao topo central. O arquivo passa ao formato novo ao salvar uma alteração.

A faixa só aparece enquanto o Euro Truck está em primeiro plano; oculta ao minimizar ou alternar de aplicativo e acompanha a posição da janela. Não captura cliques ou teclado. A primeira versão independente tem Euro Truck em janela/sem bordas como alvo de compatibilidade; tela cheia exclusiva não tem suporte geral garantido. Em janela estreita, reduz a fonte até um mínimo; se ainda não couber, sinaliza truncamento no painel e usa reticências. Selecione menos métricas ou amplie a janela nesse caso.

## Origem e atualidade das leituras

- CPU: uso total pelo Windows; temperatura pelo sensor Dell 1, ainda identificada como **CPU provável**.
- GPU: temperatura NVIDIA obtida por NVML independentemente da fonte de temperatura escolhida para o controle das fans.
- Uso GPU: NVML quando disponível; em falha, contadores Windows restritos ao identificador do adaptador NVIDIA. A alternativa Windows apresenta o motor físico mais ocupado, somando a atividade dos processos por motor e limitando a 100%; a origem aparece no painel. Não soma a GPU Intel nem motores diferentes.
- Fans: RPM das fans Dell 50 e 51 obtidos do ciclo térmico existente. Boost continua sendo um pedido de ventilação, não porcentagem comprovada de RPM.
- Jogo: eventos de apresentação da aplicação coletados pelo Windows ETW e analisados pela biblioteca PresentData vinculada estaticamente. FPS é a taxa média em uma janela recente de aproximadamente um segundo; ms é o intervalo médio entre apresentações, não duração de trabalho GPU. A captura filtra o PID do Euro Truck e seleciona uma única swapchain por atividade recente.

A apresentação atualiza a cada segundo. As leituras térmicas e de RPM seguem o ciclo existente de aproximadamente dois segundos e expiram após seis segundos sem nova amostra. Leituras auxiliares e dados do jogo expiram após três segundos. Leituras ausentes aparecem como **—** na faixa e como indisponíveis no painel, enquanto zero válido permanece zero. Perdas ETW invalidam o intervalo afetado; a captura volta a publicar ao obter um intervalo íntegro.

As consultas auxiliares e o coletor de frames usam threads próprias. Abertura, fechamento e espera pela sessão ETW não ocorrem no ciclo térmico nem na interface. A captura é solicitada quando FPS/tempo de quadro são necessários para a faixa ou quando o painel Monitoramento está visível, e desativada quando não há demanda. A sessão ETW tem identidade própria e libera somente seus recursos ao encerrar. O aplicativo normal já usa elevação para a interface Dell; acesso ETW negado é informado sem interromper sensores ou controle térmico.

O monitoramento NVIDIA pode manter a GPU acordada. Não são consultadas potência ou tensão nem enviados ajustes de clock, tensão ou limite de potência. O histórico SQLite e sua frequência não mudaram.

## Compilar e conferir

`tools/build-gui.ps1` compila o aplicativo normal, com manifesto de administrador. Para conferir o layout sem hardware ou banco de histórico, use:

```powershell
.\tools\build-gui.ps1 -OutputDirectory build/preview -Preview
.\build\preview\FanController.exe
```

Essa compilação força dados simulados, usa manifesto sem elevação e janela/instância separadas da versão instalada. Não deve ser distribuída como aplicativo de controle.

`tools/test.ps1` inclui regressões existentes e testes de seleção/migração das preferências, métricas ausentes/zero, identidade do processo, swapchains, intervalos, expiração, estado limitado e recuperação de perdas.

A biblioteca PresentData vem do subconjunto de fontes fixado em `third_party/presentmon-prototype/REVISION.txt`. A compilação usa `tools/build-presentdata.ps1`; os avisos MIT/terceiros são incluídos no instalador.

## Validação real

O jogo de referência é o Euro Truck Simulator 2 instalado em `D:\SteamLibrary\steamapps\common\Euro Truck Simulator 2`. O protótipo já mostrou a faixa com sensores e frames reais no menu do jogo, sem RTSS. A versão integrada tem validação e limites em `.scratch/osd-independente/integration-validation.md`. O host de teste `tools/test-independent-osd-live.ps1 -Run -Elevated` exercita os módulos de produção, com consultas somente leitura, sem interromper a sessão de fans instalada.
