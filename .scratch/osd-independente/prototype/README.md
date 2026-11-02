# Protótipo descartável: faixa independente

Pergunta: é possível exibir uma linha compacta sobre Euro Truck Simulator 2 e medir seus frames sem RTSS, serviço adicional ou código carregado no jogo?

Este experimento é separado do FanController instalado. Não aplica boost, perfis, clocks ou tensão; não acessa o histórico SQLite nem salva preferências. Usa fontes de monitoramento existentes apenas para leitura. Não cria tarefa de login ou serviço.

## Executar

Se recebeu o ZIP do protótipo, extraia-o e execute o EXE incluído com botão direito → Executar como administrador. Os comandos abaixo são para execução a partir do repositório.

Na raiz do projeto:

```powershell
.\tools\build-overlay-prototype.ps1
.\tools\run-overlay-prototype.ps1 -Elevated
```

Também é possível clicar com o botão direito em `build\overlay-prototype\FanController-Overlay-Prototype.exe` e escolher Executar como administrador. A elevação é necessária nesta máquina para coletar eventos ETW e consultar sensores Dell. Sem ela, o protótipo continua mostrando as métricas disponíveis e explica a falha no painel. O Windows pede UAC; o protótipo não muda permissões ou grupos para contornar essa exigência.

Abra o Euro Truck e use modo janela/sem bordas. A faixa deve aparecer apenas com ele em primeiro plano, centralizada no topo, e desaparecer ao minimizar ou trocar de aplicativo. Não deve capturar foco, cliques ou teclas. Para encerrar, use Encerrar protótipo. RTSS não é necessário; se já estiver aberto por outro motivo, o protótipo não o encerra ou altera.

## Composição

- `main.cpp`: painel de diagnóstico, seleção do processo do jogo, consultas de leitura e ciclo da faixa.
- `overlay_window.h`: janela em camadas com alfa por pixel, texto Segoe UI, fundo escuro translúcido e estilos de interação transparente. Fonte entre 13 e 10 pixels lógicos, com aviso explícito no painel se precisar truncar em janela estreita.
- `presentmon_frames.cpp`: adaptação do consumidor ETW aberto PresentData, vinculado estaticamente ao mesmo EXE. Revisão e avisos em `third_party/presentmon-prototype/`.

Cada captura ETW tem nome exclusivo do processo. O encerramento libera somente sua própria sessão. A coleta filtra o PID do jogo, seleciona a swapchain com mais apresentações recentes e descarta dados antigos ou intervalos afetados por perdas. FPS representa apresentações médias da aplicação em aproximadamente um segundo; ms é o intervalo médio entre essas apresentações. Essas medidas não equivalem automaticamente ao tempo mostrado pelo RTSS ou ao tempo de trabalho da GPU.

## Diagnóstico opcional

```powershell
.\tools\run-overlay-prototype.ps1 -Elevated -Record -Seconds 300
```

Grava explicitamente métricas e estado da faixa em `.scratch/osd-independente/prototype-live.jsonl` e encerra após cinco minutos. Sem `-Record`, não grava telemetria. CPU provável e RPM expiram em seis segundos; leituras auxiliares e frames expiram em três segundos.

O código é uma prova técnica, não uma substituição da versão instalada. A avaliação de desempenho, modos de exibição, troca de foco e qualidade da medição será registrada no relatório de validação antes de integração ao produto.
