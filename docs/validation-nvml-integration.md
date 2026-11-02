# Integração opcional de temperatura NVML

Implementada e validada em 24/10/2026, após autorização do usuário para
`--gpu-source nvml`, desligada por padrão.

## Comportamento

- `status`, `watch` e `boost` aceitam `--gpu-source dell|nvml`, uma única vez.
- Sem a opção, usa Dell; não inicializa nem carrega NVML.
- NVML é carregada sob demanda, uma vez, de `System32\nvml.dll`.
- Usa apenas inicialização, enumeração, handle, temperatura pela API atual e limpeza.
- Não usa subprocessos, não consulta potência/P-state e não instala componentes.
- Fonte e indisponibilidade são explícitas na saída; CPU continua provável, pela Dell.
- Não há fallback automático para Dell ou reaproveitamento de temperatura anterior em erro.
- Valores NVML menores ou iguais a zero e maiores que 125 são considerados implausíveis;
  o número 30 não é rejeitado globalmente.
- Inicialização que falha não é repetida em cada atualização. Erros posteriores de
  leitura são consultados novamente no próximo ciclo de watch, sem reinicializar a DLL.
- Em boost, falha inicial impede ajustes; falha de leitura após aplicar inicia restauração.
- status retorna 1 se a GPU solicitada estiver indisponível. Watch continua indicando
  o problema e retorna 1 ao encerrar caso alguma amostra tenha ficado indisponível.

A NVML pode interferir no repouso da GPU; foi mantida opcional por esse motivo.
Nenhuma curva térmica ou regra automática de redução de boost foi adicionada.

## Testes simulados

`tools/test.ps1` passou:

- 18 cenários da sessão de boost e restauração.
- Fonte Dell não chama a fábrica NVIDIA, inclusive com valor Dell de 30.
- NVML só inicializa na primeira leitura explícita e reutiliza a fonte entre amostras.
- Inicialização indisponível: informa erro, sem fallback e sem repetição por amostra.
- Falha após leitura válida: não usa valor anterior nem valor Dell; leitura posterior válida recupera.
- Validação de valores implausíveis, preservando uma leitura NVML legítima de 30.
- Fonte padrão e explícita, opções intercaladas com fans e rejeição de opções repetidas,
  desconhecidas, incompletas, fans inválidas e pedidos fora da faixa.

Esses testes simulam a fonte de temperatura/fábrica e não provocam perda do driver
real nem remoção da DLL de sistema.

## Hardware real

Executado das 22:00:39 às 22:01:03 (America/Sao_Paulo):

| Caso | Resultado | NVML no processo | Memória residente na amostra |
| --- | --- | --- | ---: |
| status com NVML | temperatura válida, código 0 | leitura efetuada | não medida |
| watch padrão | Ctrl+C, código 0 | ausente, confirmado pelos módulos | 11.620.352 bytes |
| watch com NVML | temperatura válida, Ctrl+C, código 0 | presente | 30.306.304 bytes |
| boost 30/30 com NVML | Custom, leituras, Ctrl+C, código 0 | presente | 30.355.456 bytes |

Boost começou com perfil 160 e boosts 0/0. A saída mostrou Custom e valores
30/30 durante a sessão. Ao terminar, uma consulta WMI independente confirmou
perfil 160 e boosts 0/0; o registro de sessão foi removido. A restauração externa
de emergência do harness não foi necessária.

O custo de memória observado foi aproximadamente 11,1 MiB no monitor padrão e
28,9 MiB com NVML, em execuções curtas. Não é estimativa de bateria ou garantia
de consumo durante execução prolongada; não inclui os processos do driver.

Compilação com `/W4 /WX /O2 /MT`: passou. Executável: 334.336 bytes (326,5 KiB).

## Artefatos e limites

- `src/gpu_temperature.h`: seleção e política de indisponibilidade.
- `src/nvml_temperature.h`: carregamento dinâmico e ciclo de vida NVML.
- `src/cli.h`: argumentos comuns a todos os comandos.
- `tests/gpu_temperature_test.cpp`: cenários simulados da fonte e CLI.
- `.scratch/fan-controller/check-nvml-integration.ps1`: integração elevada.
- `.scratch/fan-controller/nvml-integration-check.json`: resultados completos.
- Arquivos `gpu-default-watch-*`, `gpu-nvml-watch-*`, `gpu-nvml-boost-*` e
  `nvml-status-output.txt` na mesma pasta: saídas e métricas.

O estado de energia da GPU e consumo de bateria continuam sem quantificação
independente. Fonte GPU confiável para uma curva automática ainda exige definir
política de validade/falhas; esta integração é de exibição e monitoramento.
