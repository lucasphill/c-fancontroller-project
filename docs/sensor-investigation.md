# Identificação e confiabilidade dos sensores

Investigação em 24/10/2026 no Dell G15 5530. Nenhum ajuste de fan ou perfil foi enviado.

## Inventário

- CPU: Intel Core i5-13450HX, 10 núcleos, 16 processadores lógicos.
- GPU dedicada: NVIDIA GeForce RTX 3050 6GB Laptop GPU.
- GPU integrada: Intel UHD Graphics.

## Mapeamento

| Fan | Sensor associado pelo firmware | Interpretação | Evidência |
| --- | --- | --- | --- |
| 50 | 1 | Provável CPU | GetFanSensors e convenção do projeto tcc-g15; sem referência independente de temperatura da CPU |
| 51 | 6 | GPU NVIDIA | GetFanSensors e oito comparações com o driver NVIDIA |

`GetFanSensors` informou exatamente um sensor por fan: 1 para fan 50 e 6 para fan 51.
O vínculo firmware é confirmado; ele não determina sozinho o nome físico do sensor.

## GPU: comparações independentes

O `nvidia-smi` instalado foi usado apenas para consultas de nome, temperatura,
P-state, utilização e potência da GPU. Não foram enviados comandos de configuração.
O driver informou 46–48 graus Celsius nas oito amostras pareadas.
O sensor Dell 6 lido após cada consulta ficou entre 46 e 48, com diferença
absoluta de no máximo 1 grau em relação à NVIDIA.

Uma sequência particularmente relevante:

| Momento | Fonte | Leitura |
| --- | --- | ---: |
| Antes da consulta NVIDIA | Dell, sensor 6 | 30 °C |
| Consulta NVIDIA | Temperatura da GPU | 46 °C |
| Após a consulta NVIDIA | Dell, sensor 6 | 46 °C |

Posteriormente, sem consultas NVIDIA durante o teste de CPU, o sensor 6 voltou
a ficar em 30 graus. Isso sustenta que a leitura fixa de 30 não é uma referência
confiável nesta condição; não prova que todo valor de 30 seja inválido.

Indisponibilidade durante mudanças de estado de energia da GPU ou comportamento
do provedor Dell são hipóteses. Consultar a GPU pode alterar seu estado de energia:
o ensaio não distingue despertar da GPU de atualização do cache do provedor,
nem determina a causa exata dos 30 graus. Não atribuir automaticamente a falha
ao sensor físico ou presumir que o componente esteja realmente frio.

Há relato de leituras anormais da interface Dell no próprio projeto de referência,
inclusive relacionadas à GPU; trata-se de evidência contextual, não comprovação
da causa neste computador:
https://github.com/AlexIII/tcc-g15/issues/9

## CPU: limites da identificação

Fontes locais consultadas:

- `MSAcpi_ThermalZoneTemperature`: retornou sem suporte.
- Namespace LibreHardwareMonitor: ausente.
- Namespace OpenHardwareMonitor: ausente.

Essas verificações não são uma busca exaustiva de todo software da máquina.
Não foram instalados drivers ou programas para ler temperaturas de CPU.

Para avaliar a resposta funcional, foi criada uma thread de CPU com prioridade
baixa por no máximo 12 segundos, sem carga de GPU nem troca de perfil/fans.

| Etapa | Sensor 1: média | Faixa | Sensor 6 |
| --- | ---: | --- | --- |
| Antes | 60,67 °C | 60–63 °C | 30 °C |
| Uma thread ativa | 61,33 °C | 60–63 °C | 30 °C |
| Depois | 61,00 °C | 61 °C | 30 °C |

A diferença é pequena e as faixas se sobrepõem. O teste não confirma causalmente
que o sensor 1 mede CPU, nem calibra sua precisão ou equivalência a package/core.
O nome CPU continua provável pela associação do firmware, pela identificação
independente do sensor 6 como GPU e pela convenção usada no tcc-g15.

## Consequência para curvas e exibição

- É possível identificar fan 51/sensor 6 como GPU com boa evidência local.
- Fan 50/sensor 1 pode ser apresentado como provável CPU, sem alegação de
  temperatura de núcleo/package validada por outra fonte.
- Uma leitura GPU isolada de 30 graus não deve ser usada silenciosamente como
  temperatura válida para reduzir ventilação numa curva.
- Não implementar um filtro global que descarte sempre o número 30: esse valor
  pode ser legítimo em outras condições. Validade precisa considerar fonte e estado.
- Uma integração futura pode consultar NVML diretamente, sem chamar nvidia-smi
  a cada amostra. A biblioteca vem do driver NVIDIA, mas disponibilidade, erros,
  consumo e efeitos sobre suspensão da GPU precisam ser avaliados.
- Nenhuma curva automática ou mudança de fonte do executável foi implementada
  nesta investigação. O programa continua mostrando os IDs e valores Dell brutos.

## Artefatos e referências

- `tools/inspect-sensors.ps1`: inventário, relações e comparação Dell/NVIDIA.
- `.scratch/fan-controller/sensor-investigation.json`: 16 amostras, oito pareadas.
- `tools/check-cpu-sensor.ps1`: observação antes/durante/depois da atividade CPU.
- `.scratch/fan-controller/cpu-sensor-check.json`: 20 amostras.
- Todas as amostras de CPU mantiveram perfil 160 e boosts 0/0.
- Descrição de GetFanSensors: https://docs.kernel.org/wmi/devices/alienware-wmi.html
- Convenção CPU/GPU: https://github.com/AlexIII/tcc-g15/blob/master/src/Backend/AWCCThermal.py
- Fonte NVIDIA: https://docs.nvidia.com/deploy/nvidia-smi/index.html

A documentação NVIDIA recomenda NVML para ferramentas mantidas ao longo de
versões do driver, em vez de depender do formato da saída do nvidia-smi.
