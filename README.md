# FanController

[Baixar a build mais recente para Windows x64](https://github.com/lucasphill/c-fancontroller-project/releases/latest).
Cada push em `main` executa os testes sem hardware, compila a GUI e a CLI e
publica uma nova release com `FanController.exe`, `fanctl.exe`, ZIP com licenças
e checksums SHA-256. Pull requests executam a mesma validação sem publicar.
Também é possível executar manualmente o workflow **Windows build and release**.
As releases automáticas são portáteis: não incluem instalador nem configuram
início automático. O instalador descrito abaixo é gerado localmente.

Aplicativo C++ para este Dell G15 5530, com janela, bandeja e comandos de terminal,
com foco em melhorar a refrigeração por boost individual das fans.

## Interface gráfica

Novo painel escuro e redimensionável com monitoramento ao vivo, gráficos recentes e
faixa OSD própria, sem exigir RTSS ou MSI Afterburner. Selecione as métricas na aba
**OSD**. [Monitoramento e exibição na tela](docs/monitoring-osd.md).

Instalador: `dist\FanController-Setup-0.6.0-x64.exe`. Instala em Arquivos de
Programas e inicia automaticamente na bandeja após login, retomando o último
modo aplicado. Primeira instalação: Normal. [Instalação e início automático](docs/installation.md).

Abra `build\FanController.exe` com duplo clique e confirme a solicitação de
administrador do Windows. Nenhum terminal precisa ficar aberto.

- Ao abrir, apenas monitora: não aplica os valores dos controles automaticamente.
- Selecione **Manual**, **Normal** ou **Performance** e clique em **Aplicar**.
- Em Manual, ajuste os boosts das fans 50 e 51. Nos perfis automáticos, cada fan
  segue sua curva de temperatura, com boost mínimo 30.
- A aba **Curvas** permite editar os pontos e **Salvar e aplicar** os ajustes.
- A aba **Histórico** mostra gráficos separados de CPU e GPU, com temperatura,
  boost lido e RPM. Registra ao iniciar e a cada cinco minutos, inclusive na
  bandeja, e mantém os últimos 90 dias em SQLite local. [Histórico térmico](docs/thermal-history.md).
- **Máximo (100)** aplica boost 100 nas duas fans.
- **Restaurar** devolve o perfil e os boosts anteriores e encerra o controle manual.
- Fechar no **X** ou minimizar mantém o programa na bandeja, perto do relógio.
- Clique no ícone para reabrir; o menu do botão direito também permite restaurar e sair.
- **Sair** tenta restaurar os ajustes antes de encerrar. Se não confirmar a
  restauração, mantém a janela aberta, preserva o registro e oferece nova tentativa.

A opção NVML começa desmarcada. Para mudar a fonte GPU durante controle ativo,
restaure os ajustes primeiro. Normal e Performance ativam NVML durante o controle.
O novo monitoramento consulta temperatura e uso NVIDIA separadamente dessa opção,
sem mudar a fonte de controle; essas consultas também podem manter a GPU acordada.
As curvas e o último modo aplicado ficam salvos junto ao executável. Na versão
instalada, a tarefa de login retoma o modo salvo na bandeja, sem UAC a cada login.
O segundo plano funciona durante a sessão do usuário conectado.
Não é um serviço do Windows e não mantém controle antes do login ou após encerrar a sessão.

Se a janela não estiver visível, procure o ícone do FanController na seta de ícones
ocultos da bandeja. Abrir novamente o executável também traz a janela existente.
CLI e janela compartilham o controle e recusam duas sessões manuais simultâneas.

Detalhes e validação: [interface gráfica](docs/gui.md).
Curvas, proteções e edição: [perfis automáticos](docs/automatic-profiles.md).

Leitura e ajuste de boost por COM/WMI diretamente, sem PowerShell
em tempo de execução, driver adicional, serviço ou dependência do aplicativo AWCC.
O firmware precisa expor a interface Dell `AWCCWmiMethodFunction`.

## Compilar

Com MSVC x64 e Windows SDK instalados (consulte [toolchain](docs/toolchain.md)):

```powershell
.\tools\build.ps1
.\tools\build-gui.ps1
```

Os executáveis ficam em `build\fanctl.exe` (CLI) e `build\FanController.exe` (GUI).
O runtime C++ é vinculado estaticamente;
as ferramentas de compilação não precisam acompanhar o executável.

## Usar

Em um terminal aberto como administrador:

```powershell
.\build\fanctl.exe status
.\build\fanctl.exe watch
.\build\fanctl.exe boost --fan 50=30 --fan 51=30
```

`status` imprime uma amostra e encerra. `watch` aguarda 2 segundos entre amostras e
termina com Ctrl+C. As consultas têm espera limitada; a saída pode aguardar a
consulta atual. Uma falha encerra o comando com código 1; argumentos inválidos
retornam 2. Ajuda: `fanctl --help`, sem exigir administrador.

`boost` aceita uma ou duas fans (IDs 50 e 51), com valores inteiros de 0 a 100.
Para solicitar o máximo dessa faixa nas duas fans:

```powershell
.\build\fanctl.exe boost --fan 50=100 --fan 51=100
```

O executável principal `build\fanctl.exe` já foi atualizado para a faixa 0–100.

100 é o valor de boost solicitado, não uma confirmação de 100% da rotação física.
Acompanhe os RPM para verificar a resposta. Os pontos 0, 30, 50, 75 e 100 foram
[medidos neste computador](docs/fan-calibration.md), em Custom. No ensaio, boost
100 produziu aproximadamente 5.000 RPM nas duas fans. A correspondência pode
variar com temperatura, carga e firmware.
Uma fan omitida conserva seu ajuste.
Uma [comparação de perfis](docs/profile-comparison.md) mostrou RPM maiores no
perfil Custom com o mesmo boost 30. O comando entra automaticamente em Custom;
o ganho térmico sob carga padronizada ainda precisa ser medido.
Exemplo para ajustar apenas uma fan:

```powershell
.\build\fanctl.exe boost --fan 50=30
```

A sessão permanece no terminal e imprime as leituras. Pressione **Ctrl+C** para
tentar restaurar o perfil e os boosts anteriores e sair. A restauração é confirmada por
leitura; código 3 indica que ela não pôde ser confirmada. Falhas de leitura ou
aplicação encerram a sessão e também iniciam a tentativa de restauração.

O comando aceita iniciar no perfil equilibrado (160) ou Custom (0), salva esse
estado e seleciona Custom durante a sessão. Ao sair, restaura primeiro o perfil
anterior e depois os boosts das duas fans, pois trocar perfil pode alterar seus
ajustes. Uma fan omitida conserva seu boost solicitado anterior, mas a troca de
perfil é global e pode alterar seu comportamento automático.
Use um único programa de controle de fans durante a sessão. O fanctl bloqueia
outras sessões de boost dele próprio, mas não encerra programas ou serviços.

Antes de escrever, salva `fanctl-session.txt` junto ao executável. O arquivo é
removido somente após confirmar os boosts e o perfil originais. Se ele já
existir, um novo boost é recusado: preserve o registro e compare seus valores
com `fanctl status` antes de qualquer recuperação manual.

Encerramento forçado do processo, fechamento da janela, perda de energia ou
travamento não garantem restauração. A recuperação automática de uma sessão
interrompida ainda não está implementada.

A [investigação de sensores](docs/sensor-investigation.md) associa fan 51/sensor 6
à GPU NVIDIA e fan 50/sensor 1 à provável CPU. A exibição atual ainda usa os IDs
e valores Dell brutos; leituras GPU isoladas de 30 °C não foram confirmadas.
Boost é o ajuste solicitado ao firmware, não uma porcentagem da rotação máxima.
Temperaturas pontuais não medem ganho de refrigeração.

### Temperatura GPU opcional pela NVIDIA

Por padrão, a GPU continua usando a leitura Dell, identificada como não validada
pela NVIDIA. O programa não carrega a NVML nesse modo. Para consultar diretamente
o driver NVIDIA, acrescente `--gpu-source nvml` ao comando:

```powershell
.\build\fanctl.exe status --gpu-source nvml
.\build\fanctl.exe watch --gpu-source nvml
.\build\fanctl.exe boost --fan 50=100 --fan 51=100 --gpu-source nvml
```

A saída mostra `GPU NVIDIA: ... C [NVML]`. O sensor 1 é identificado como CPU
provável, ainda lido da Dell. `--gpu-source dell` seleciona explicitamente o padrão.
As opções vêm depois do comando; em boost, podem ser intercaladas com `--fan`.

A NVML é carregada uma vez, sob demanda, da DLL instalada no diretório de sistema.
Somente temperatura é consultada: sem subprocessos nvidia-smi, telemetria de
potência ou instalação adicional. O acesso pode interferir no repouso da GPU;
o impacto em bateria ainda não foi quantificado. Veja
[a avaliação](docs/nvml-investigation.md).

Se a NVML falhar, a saída indica `indisponivel [NVML]` e a causa, sem reutilizar
temperatura antiga nem trocar silenciosamente para a Dell. `status` retorna 1.
`watch` continua exibindo a indisponibilidade e retorna 1 ao sair se houve falha;
uma falha de inicialização exige reiniciar o comando para nova tentativa.
Em `boost`, indisponibilidade inicial recusa ajustes; falha durante a sessão
interrompe o controle e tenta restaurar o estado anterior. Nenhuma curva
automática foi adicionada nesta etapa.

## Verificações

```powershell
.\tools\test.ps1
```

Os testes usam um provedor simulado e não acessam o hardware. Incluem falha
parcial após escrita, leitura indisponível, restauração incompleta e cancelamento.
Também verificam seleção da fonte GPU, inicialização sob demanda, falhas sem
fallback, valores inválidos e argumentos da linha de comando.
O comando C++ também foi validado nas fans reais, com entrada em Custom, restauração por Ctrl+C e
consulta independente do estado final. Consulte
[a validação da correção](docs/validation-custom-cli.md).
A opção NVML foi validada em status, watch e boost com confirmação independente
da restauração: [validação da integração](docs/validation-nvml-integration.md).

## Referências da interface

- [Descrição da interface no kernel Linux](https://docs.kernel.org/wmi/devices/alienware-wmi.html)
- [Investigação WMI do tcc-g15](https://github.com/AlexIII/tcc-g15/blob/master/WMI-AWCC-doc.md)
- [Descoberta de recursos no AlienFan SDK](https://github.com/T-Troll/alienfx-tools/blob/master/alienfan-tools/alienfan-SDK/alienfan-SDK.cpp)

As referências são obtidas por engenharia reversa; as leituras precisam ser
validadas neste computador. Decisões e evidências locais ficam em
[spec.md](.scratch/fan-controller/spec.md).
