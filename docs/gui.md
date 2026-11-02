# Interface gráfica e bandeja

Primeira versão implementada em 24/10/2026, conforme as decisões locais em
`.scratch/fan-controller/gui-plan.md`.

## Uso

Na versão 0.6.0, a janela é escura e redimensionável e abre na aba
**Monitoramento**. A aba **OSD** configura a faixa própria, sem RTSS; consulte
[monitoramento e exibição na tela](monitoring-osd.md). O monitoramento auxiliar
NVIDIA é independente da caixa de seleção que escolhe a fonte GPU para controle.

Executável: `build\FanController.exe`. Pode ser aberto pelo Explorer sem terminal.
O manifesto pede elevação para consultar e controlar a interface Dell.
Uma instância por sessão de usuário; outra abertura traz a janela existente.

A janela mostra RPM, boosts atuais, CPU provável pela Dell e temperatura GPU com
fonte explícita. Controles de boost são pedidos de 0–100, não RPM exatos nem
porcentagem comprovada do máximo físico.

- Aplicar: entra em Custom e aplica os dois valores escolhidos.
- Máximo (100): preenche e aplica 100 nas duas fans.
- Restaurar: volta ao perfil e boosts anteriores, encerrando o controle manual.
- X/minimizar: oculta a janela e mantém a atividade na bandeja.
- Ícone da bandeja: abre a janela; botão direito oferece abrir, restaurar e sair.
- Sair: tenta restaurar; se não confirmar, mantém a janela e o registro disponíveis
  para nova tentativa. Não há encerramento silencioso nesse caso.

Ao reaplicar, a sessão anterior é restaurada antes de iniciar outra. Se isso
falhar, o novo pedido é recusado. Mudar fonte GPU requer restaurar o controle
primeiro. A NVML começa desligada e pode interferir no repouso da GPU quando ligada.

O aplicativo começa apenas monitorando. Salva curvas de Normal/Performance,
e a versão instalada cria uma tarefa de login para retomar o último modo na
bandeja. A abertura normal apenas monitora; `--startup` retoma o modo salvo.
Não há serviço. Ele permanece no segundo plano enquanto a sessão
do usuário estiver em execução. Terminar pelo Gerenciador de Tarefas ou perder
energia continua sem garantia de restauração.

## Implementação

- Win32, controles comuns Windows, C++17, runtime estático.
- Subsistema Windows e entrada wWinMain: nenhuma console é criada pelo aplicativo.
- Interface com Segoe UI e escala DPI PerMonitorV2.
- Ícone próprio gerado no aplicativo; sem recursos bitmap externos ou framework web.
- Shell_NotifyIcon com recuperação do ícone após reinício do Explorer.
- Thread de trabalho para WMI/NVML e ajustes; thread da janela somente trata UI.
- COM e objetos WMI pertencem à thread de trabalho que os inicializa.
- Leituras a cada dois segundos; mensagens e comandos sincronizados por mutex,
  condição e fila. Cancelamento pode interromper aplicação entre comandos.
- `src/windows_backend.h` compartilha Dell WMI, trava e persistência com o CLI.
- `src/control_session.h` compartilha aplicação, checagem GPU e restauração.
- A trava global impede controle simultâneo GUI/CLI. O registro é o mesmo
  `fanctl-session.txt` junto aos executáveis.

## Verificação

Compilação com `/W4 /WX /O2 /MT` passou para GUI e CLI.
Executável gráfico: 272.384 bytes (266 KiB).

`tools/test.ps1` passou com:

- 18 cenários de BoostSession.
- Seleção de fonte GPU e argumentos CLI, incluindo ausência de carga NVML por padrão.
- 7 cenários de ControlSession: persistir antes de escrever, falha do registro,
  restauração incompleta com lease/registro preservados, nova tentativa bem-sucedida,
  falha de limpeza do registro, GPU indisponível antes/durante controle e cancelamento.

Validação visual e manual na máquina:

- Janela abriu sem console, após UAC confirmado pelo usuário.
- Primeiro estado observado: monitorando, boosts 0, NVML desmarcada.
- Captura posterior confirmou controle ativo em Custom, boosts 100/100 e RPM
  aproximadamente 4.991/5.004. Registro guardou perfil160 e boosts0/0 originais.
- O usuário confirmou que X ocultou, reabriu pela bandeja e Restaurar voltou
  ao estado Monitorando.
- O registro de controle ativo foi removido após essa restauração; a remoção
  é condicionada à verificação do estado original no módulo compartilhado.
- O usuário validou os cliques. A ferramenta capturou a janela elevada, mas
  o clique automatizado não acionou o botão. Não houve tentativa de contornar UAC/UIPI.

Uma amostra inicial com NVML desmarcada mostrou memória residente perto de 21 MiB.
Isso não é garantia de consumo em execução prolongada nem inclui os processos WMI.

A falha real de firmware/restauração não foi provocada na interface; os caminhos
de erro/repetição foram verificados nos testes do módulo compartilhado e na
implementação da UI. Saída por Sair durante controle ativo não foi exercitada
manualmente nesta sessão; usa o mesmo caminho de restauração do botão Restaurar.

## Limites

- Suspensão e hibernação ainda não têm protocolo específico de retomada.
- Logoff/desligamento solicitam restauração; o início `--startup` recupera um
  registro válido de sessão interrompida antes de retomar. Falha de recuperação
  impede novo controle. Detalhes em [instalação](installation.md).
- CPU permanece provável, sem referência independente de temperatura.
- Os novos [perfis automáticos](automatic-profiles.md) têm validação de lógica;
  validação visual/hardware e ganho térmico sob carga padronizada estão pendentes.

## Compilar

```powershell
.\tools\build.ps1
.\tools\build-gui.ps1
.\tools\test.ps1
```
## Histórico

A aba Histórico oferece gráficos de temperatura, boost lido e RPM para CPU e GPU,
com coleta pontual a cada cinco minutos e retenção local de 90 dias.
Veja [histórico térmico](thermal-history.md) para fontes, escalas e comportamento
em falhas.

