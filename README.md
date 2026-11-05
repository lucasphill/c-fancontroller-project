# aeri

[Baixar a build mais recente para Windows x64](https://github.com/lucasphill/c-fancontroller-project/releases/latest).
Cada push em `main` executa os testes sem hardware, compila a GUI e a CLI e
publica uma nova release com `aeri.exe`, `fanctl.exe`, ZIP com licenças
e checksums SHA-256. Pull requests executam a mesma validação sem publicar.
Também é possível executar manualmente o workflow **Windows build and release**.
As releases automáticas são portáteis: não incluem instalador nem configuram
início automático. O instalador descrito abaixo é gerado localmente.

Aplicativo C++ para este Dell G15 5530, com janela, bandeja e comandos de terminal,
com foco em melhorar a refrigeração por boost individual das fans.

## Interface gráfica

A **aeri** usa o layout Estúdio: navegação lateral, temperaturas e RPM em destaque,
gráficos com detalhes da medição sob o cursor e fundo com vidro opcional.
[Interface, requisitos e validação](docs/aeri.md).

- **Visão geral:** acompanhamento de CPU e GPU; FPS fica somente no OSD durante jogos.
- **Histórico:** temperatura e RPM em gráficos alinhados, filtro por componente,
  zoom e eventos de ativação/alteração de perfis com a configuração preservada.
  Coleta nominal a cada 2 segundos, detalhe por 24 horas e resumos por minuto
  até 90 dias, conservando mínimos e máximos.
- **Perfis e curvas:** editor gráfico com 2 a 16 pontos, aplicação explícita,
  ativação de Normal/Performance e acesso ao controle manual/restauração.
- **Configurações:** transparência e seleção das métricas da faixa OSD.

A interface requer o runtime **Microsoft Edge WebView2 Evergreen**. Seus recursos
são locais e embutidos no executável. A aquisição e o controle continuam no núcleo
C++, separados da apresentação. [Decisão técnica](docs/adr/0004-webview2-para-interface-aeri.md).

Abra `build\aeri.exe` após compilar e confirme o pedido de administrador.
A abertura normal apenas monitora. Fechar/minimizar mantém o programa na bandeja;
**Encerrar** tenta restaurar o estado anterior antes de sair. Uma restauração não
confirmada mantém o registro disponível para nova tentativa.

O rebranding preserva o banco em `%LOCALAPPDATA%\FanController\history.db`, os
arquivos de configuração, o alias `FanController.exe` e a tarefa de início
existente. O instalador local passa a se chamar `aeri-Setup-0.7.0-x64.exe`.
As releases automáticas continuam portáteis.

Para avaliar o visual sem hardware:

```powershell
.\tools\build-gui.ps1 -Preview -OutputDirectory build/aeri-preview
.\build\aeri-preview\aeri.exe
```

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
