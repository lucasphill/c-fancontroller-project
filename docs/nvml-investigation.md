# Avaliação da NVML local

Executada em 24/10/2026 no Dell G15 5530 com RTX 3050 6GB Laptop GPU.
Não foram instalados SDKs, programas ou drivers, nem enviados comandos de
configuração de GPU, fan ou perfil.

## Disponibilidade e implementação experimental

- DLL encontrada em `C:\Windows\System32\nvml.dll`, versão 8.17.15.9597.
- Assinatura Authenticode validada, com publicador WHQL Microsoft.
- Sonda C++ x64 com carregamento dinâmico apenas do diretório de sistema.
- Chamadas usadas: inicialização/limpeza, contagem e handle de dispositivo,
  temperatura, P-state e potência. Nenhum setter foi carregado.
- A enumeração foi limitada a uma GPU NVIDIA, conforme o inventário local.
- A sonda é separada do fanctl; o executável principal não foi alterado.

## Precisão da leitura e compatibilidade

A função legada `nvmlDeviceGetTemperature` funcionou em 15/15 amostras.
A função atual `nvmlDeviceGetTemperatureV` também funcionou em 15/15, com estrutura
de versão 1 e ABI conferida no header oficial NVIDIA. Na verificação conjunta,
as duas APIs devolveram os mesmos valores em todas as amostras.

Seis consultas de referência ao nvidia-smi, apenas de temperatura, foram
comparadas com a amostra NVML mais próxima no tempo: diferença máxima de 1 grau.
As consultas não foram simultâneas. Isso valida a ligação da API e consistência
com a ferramenta NVIDIA; o nvidia-smi também usa NVML e não é um termômetro
físico independente. O ensaio não calibra precisão absoluta nem garante frescor
da leitura quando o dispositivo está suspenso.

Durante a etapa só de temperatura, a NVML informou majoritariamente 48 graus,
enquanto a Dell apresentou leituras de 54–55. Na etapa com telemetria e referência
NVIDIA, os valores ficaram próximos de 55–56. As fases sequenciais e mudanças
de estado de energia impedem concluir qual diferença seria cache, atualização
do provedor ou mudança física de temperatura. Não afirmar concordância permanente
entre Dell e NVIDIA, nem descartar todo valor de 30 por regra fixa.

## Custo observado do processo de teste

| Modo | Memória residente | CPU acumulada antes da limpeza | Duração | Tempo médio das chamadas por amostra |
| --- | ---: | ---: | ---: | ---: |
| Referência, sem carregar NVML | 4,32 MiB | 31,25 ms | 30,15 s | sem consultas |
| Inicialização NO_ATTACH, sem obter handle | 24,17 MiB | 31,25 ms | 31,85 s | sem consultas |
| Só temperatura, função legada | 24,24 MiB | 62,50 ms | 31,87 s | 0,0445 ms |
| Temperatura, potência e P-state | 24,34 MiB | 78,13 ms | 38,14 s | 492,69 ms |
| Duas APIs de temperatura, legada e atual | 24,25 MiB | 78,13 ms | 30,83 s | 0,0611 ms, somando ambas |

A inicialização levou aproximadamente 0,62–1,70 segundo nas execuções medidas.
Os tempos de CPU incluem inicialização e consultas; são acumulados do processo,
não percentuais de uso da máquina. Memória e custo dos processos do driver e
do script de acompanhamento não estão incluídos. A amostra curta não é um
benchmark de bateria ou de execução prolongada.

Seis subprocessos nvidia-smi consultando temperatura, P-state e potência levaram
aproximadamente 377–609 ms cada. Esses pedidos contêm mais informação que uma
consulta isolada de temperatura; não usar essa comparação como fator exato de
aceleração. A integração direta elimina a criação do subprocesso por atualização.

A telemetria de potência foi muito mais lenta que somente temperatura nesta
plataforma. Seu custo não deve ser atribuído à consulta de temperatura. Não
recomendo incluí-la no caminho normal de atualização de um monitor leve.

## Energia e repouso da GPU

Na referência sem NVML, todas as 15 leituras Dell do sensor 6 foram 30 graus.
Durante a inicialização NO_ATTACH e as leituras NVML, surgiram temperaturas Dell
na faixa de 50–56. Ao final do experimento principal, a Dell voltou a 30 graus.
Esse comportamento é compatível com interferência no repouso da GPU, porém
o sensor Dell de 30 não comprova sozinho um estado D3, nem estabelece causalidade
exclusiva da sonda. A atividade do computador não foi padronizada e houve pausas
para corrigir os scripts experimentais.

A documentação NVIDIA informa que obter um handle pode inicializar a GPU.
O modo NO_ATTACH teve sucesso na inicialização, mas não permitiu enumerar a GPU
na primeira tentativa de leitura; a inicialização normal permitiu obter o handle.
Não presumir que NO_ATTACH garanta leituras sem despertar o dispositivo.

Com a telemetria ativa, o driver retornou P0 e potência entre 8,323 e 17,007 W.
Isso é potência reportada durante o ensaio, **não um aumento de 8–17 W causado
pela NVML**. Não temos uma medição independente de potência da GPU suspensa.
Logo, não foi quantificado o consumo adicional ou impacto em bateria.

## Recomendação

A NVML é viável como fonte opcional de temperatura, com carregamento sob demanda
e chamada direta à API atual. Manter desligada por padrão evita transformar toda
consulta simples de status em acesso automático à GPU. Erros devem resultar em
leitura indisponível explícita, sem converter zero ou temperatura Dell não
confirmada em dado confiável para curvas. A política definitiva ainda depende
da decisão do usuário; nenhuma integração ao fanctl foi feita neste ensaio.

## Estado final e reprodução

Os experimentos principal e de API atual terminaram com perfil 160 e boosts 0/0,
iguais aos estados iniciais, por leitura independente WMI. Nenhum ajuste foi escrito.

- Compilar: `tools/build-nvml-probe.ps1`.
- Sonda: `tools/nvml-probe.cpp`, saída em `build/nvml-probe.exe`.
- Avaliação: `tools/test-nvml.ps1` e `tools/test-nvml-modern.ps1`, elevados para ler WMI Dell.
- Dados: `.scratch/fan-controller/nvml-investigation.json`, `nvml-modern-check.json`,
  `nvml-*-metrics.json` e `nvml-*-samples.csv`.
- O primeiro acompanhamento perdeu o código de saída da referência, apesar de a
  sonda concluir e persistir métricas; retenção do handle foi corrigida e verificada.
- A etapa de leitura com NO_ATTACH falhou ao enumerar a GPU; os dados anteriores
  foram preservados e as etapas de leitura retomadas com inicialização normal.
- Os artefatos `nvml-investigation-attempt1.json` e `nvml-investigation-attempt2.json`
  preservam essas tentativas, sem apagar as evidências.

## Referências primárias

- Inicialização e flags: https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlInitializationAndCleanup.html
- Consultas e inicialização por handle: https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlDeviceQueries.html
- Estrutura de temperatura: https://github.com/NVIDIA/go-nvml/blob/main/pkg/nvml/nvml.h
- Relação entre nvidia-smi e NVML: https://docs.nvidia.com/deploy/nvidia-smi/index.html
