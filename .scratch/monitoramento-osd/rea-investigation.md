# Investigação REA: MSI Afterburner e RTSS

Data: 2026-10-28. Inspeção estática, sem executar ou alterar os programas instalados.

## Instalações e versões

- MSI Afterburner: `C:\Program Files (x86)\MSI Afterburner`. Registro de desinstalação: 4.6.6. Metadados de `MSIAfterburner.exe`: 4.6.6.16757.
- RivaTuner Statistics Server (RTSS): `C:\Program Files (x86)\RivaTuner Statistics Server`. **Divergência**: o registro de desinstalação anuncia 7.3.7, mas `RTSS.exe` informa 7.3.5.28314. Não foi estabelecida a causa nem qual versão está efetivamente em execução.

As versões acima são observações do registro/arquivos por PowerShell, não resultados de execução dos aplicativos.

## Evidências do MCP REA

Operação: `inspect_desktop_application`, com `detail: summary`, nos dois diretórios.

| Aplicativo | Evidência completa retida | Evidência de resumo |
| --- | --- | --- |
| MSI Afterburner | `ev_54fd0fc69620064b85e2420fdedf52f609e5c6a8af5a2ff01dce5cb7033e6593` | `ev_251f0c381b2217f4a69898ba1b88ab2009181fbdc15a525b7a9312bd370251e6` |
| RTSS | `ev_8b5d9b8635917bb5580b68c96e38075c2c280a18dc89208157bed2fa307f362b` | `ev_f43f3092d9a67433eae160d2f871d653db6d4ac3a8d140998256e91fe254c0dd` |

O inventário do Afterburner examinou 4.123 entradas e sondou 26 arquivos (8.212 bytes). O do RTSS examinou 2.312 entradas e sondou 59 arquivos (19.240 bytes). Ambos retornaram cobertura `complete-within-scan`, sem problemas de sondagem. Os identificadores retidos pertencem à sessão REA e não substituem um pacote exportado de evidências.

O RTSS apresentou oito candidatos PE com diretório CLI, incluindo `Plugins\Client\LHMDataProvider\Exporter.dll` e `SDK\Include\LHM\Importer.dll`. Isso não demonstra execução CLR ou validade completa dos metadados. Não foi necessário abrir uma sessão de descompilação Ghidra para esta investigação inicial.

## Interfaces distribuídas com os programas

Os caminhos e contratos desta seção foram lidos diretamente dos SDKs instalados, por ferramentas comuns de arquivos. São fontes distribuídas com os artefatos; não são pseudocódigo inferido pelo REA.

### Telemetria do Afterburner

- Cabeçalho: `C:\Program Files (x86)\MSI Afterburner\SDK\Include\MAHMSharedMemory.h`.
- Exemplo: `C:\Program Files (x86)\MSI Afterburner\SDK\Samples\SharedMemory\MAHMSharedMemorySample\MAHMSharedMemorySampleDlg.cpp`.
- Descrição de fontes de monitoramento: `C:\Program Files (x86)\MSI Afterburner\SDK\Include\MSIAfterburnerMonitoringSourceDesc.h`.
- Memória compartilhada: `MAHMSharedMemory`; abertura no exemplo, linha 201.
- Cabeçalho informa assinatura `MAHM` (válida), `0xDEAD` (desalocação), versão, tamanho do cabeçalho, quantidade/tamanho das entradas e instante da última coleta. O instante tem representação de 32 bits no contrato distribuído.
- Entradas informam nome, unidade, valor float, limites de gráfico, flags, identificador de fonte e índice de GPU. `FLT_MAX` representa valor indisponível.
- Há identificadores para temperaturas CPU/GPU/memória/VRM, utilização CPU/GPU, clocks, potência CPU/GPU, fans em RPM, RAM/VRAM, FPS, frametime, estatísticas de FPS e grupos de plugins.

Esses identificadores descrevem possibilidades da interface. Não comprovam que cada sensor exista neste computador, esteja habilitado ou publique dados válidos. Consumir MAHM depende do Afterburner publicando sua memória compartilhada. O arquivo separado `MACMSharedMemory.h` existe para controle; não foi necessário usá-lo ou alterar ajustes do MSI.

### Publicação de OSD no RTSS

- Cabeçalho: `C:\Program Files (x86)\RivaTuner Statistics Server\SDK\Include\RTSSSharedMemory.h`.
- Exemplo: `C:\Program Files (x86)\RivaTuner Statistics Server\SDK\Samples\SharedMemory\RTSSSharedMemorySample\RTSSSharedMemorySampleDlg.cpp`.
- `UpdateOSD`, linhas 377–456, usa `RTSSSharedMemoryV2`, valida assinatura e versão, localiza ou reserva um slot com proprietário próprio e atualiza o contador `dwOSDFrame`.
- A busca começa no índice 1; o exemplo reserva o primeiro slot aos clientes primários, como MSI Afterburner. Uma integração do FanController deve possuir e liberar somente seu próprio slot.
- O exemplo seleciona buffer estendido de 4.096 caracteres a partir de v2.7 e bloqueio por `dwBusy` a partir de v2.14, conforme a versão do protocolo, não a versão comercial do aplicativo.
- `ReleaseOSD`, a partir da linha 458, limpa slots que pertencem ao cliente e sinaliza atualização.
- O cabeçalho também descreve entradas de aplicação com dados de frames/intervalo e frametime. A coleta e apresentação reais não foram testadas.

O SDK inclui ainda `SDK\Tools\DesktopOverlayHost`, `SDK\Tools\DesktopOverlayHostLoader`, `SDK\Tools\PresentMonDataProvider`, `SDK\Tools\LHMDataProvider` e `SDK\Include\LHM\LibreHardwareMonitorLib.dll`. A presença dessas ferramentas indica opções para investigar; não estabelece que estão configuradas ou funcionando.

## Consequências para o desenho

1. Existe um caminho distribuído e explícito para publicar os dados do FanController no OSD do RTSS, usando um slot próprio.
2. Publicar OSD pelo RTSS e obter telemetria do Afterburner são decisões separadas. O FanController pode fornecer suas próprias leituras ao RTSS; usar MAHM introduz dependência do Afterburner.
3. Tratar sensor ausente/desatualizado como indisponível, preservando unidade e procedência, deve fazer parte do contrato de telemetria.
4. Recursos de interface, suporte por jogo, modo de tela, dados disponíveis nesta máquina e comportamento real do OSD permanecem questões para investigação/validação posterior.

## Limitações

O inventário REA cobre caminhos, metadados e intervalos lidos; seus hashes de diretório não são hashes de todo o conteúdo de todos os arquivos. A inspeção não constitui uma captura atômica. Não foram observados processos, telas, jogos, sensores ao vivo ou memórias compartilhadas. Nenhum ajuste de fan, clock, tensão ou perfil foi alterado. Nenhuma decisão de implementação foi tomada por este relatório.
