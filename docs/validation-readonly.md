# Validação de status e watch

Executada em 2026-10-24 neste Dell G15 5530, Windows, sem ajustes térmicos.

## Resultado

- Compilação x64 com MSVC, C++17, otimização e avisos tratados como erros: passou.
- Executável: 278.528 bytes (272 KiB).
- Ajuda sem elevação: retorno 0.
- Comando inválido: retorno 2.
- Status sem elevação: retorno 1 e orientação para abrir terminal como administrador.
- Status com elevação: retorno 0; leu fans 50/51, sensores 1/6, boost e perfil.
- Watch com elevação: várias amostras, espera de 2 segundos entre leituras,
  saída normal por Ctrl+C e retorno 0.
- Também foi verificada saída por Ctrl+Break, com retorno 0.
- Nenhum método de controle está exposto no backend desta etapa.

## Consumo observado

Na execução curta de watch validada com Ctrl+C:

- Janela antes do sinal: 6.500 ms.
- Tempo de CPU acumulado do processo: 15,625 ms.
- Memória residente: 11.030.528 bytes, aproximadamente 10,5 MiB.

Essas medidas descrevem uma amostra curta do processo fanctl. Não incluem
o custo dos processos do provedor WMI, nem estabelecem limite de consumo de longo prazo.
Não foi medida melhora de temperatura, pois nenhum boost foi aplicado.

## Artefatos reproduzíveis

- Build: `tools/build.ps1`.
- Sonda independente: `tools/probe-dell-wmi.ps1`.
- Programa de teste de console e métricas: `.scratch/fan-controller/watch-check.cpp`.
- Execução elevada dos testes: `.scratch/fan-controller/run-readonly-check.ps1`.
- Resultados: `readonly-check.json`, `status-output.txt`, `watch-output.txt`
  e `watch-metrics.json`, dentro de `.scratch/fan-controller/`.

O teste de console cria uma console isolada para o monitor e envia Ctrl+C apenas
nessa console. O harness e a prova de toolchain são ferramentas de validação;
não precisam acompanhar o executável fanctl.

## Limites pendentes

- Associação dos sensores e fans com CPU/GPU.
- Semântica e limites de escrita de boost, exigência de troca de perfil e restauração.
- Comportamento em suspensão/retomada e medições prolongadas.
