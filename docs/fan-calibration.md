# Calibração de boost e RPM

Início: 24/10/2026 21:35:14 -03:00. Fim: 24/10/2026 21:37:56 -03:00. Horários de São Paulo.

Dell G15 5530, perfil Custom (0), os mesmos valores solicitados às duas fans.
Nenhuma carga artificial foi iniciada; a atividade do computador não foi padronizada.

| Boost | Fan 50: média RPM | Fan 51: média RPM | Amostras no nível | Estabilidade final |
| ---: | ---: | ---: | ---: | --- |
| 0 | 2.017,0 | 2.303,4 | 15 | Critério atingido |
| 30 | 2.899,0 | 3.111,4 | 15 | Critério atingido |
| 50 | 3.483,6 | 3.654,0 | 15 | Critério atingido |
| 75 | 4.261,6 | 4.331,8 | 15 | Critério atingido |
| 100 | 5.006,2 | 4.998,8 | 15 | Critério atingido |

## Método e interpretação

- Cada nível foi observado por pelo menos 15 amostras com espera de 2 segundos entre elas; limite de 30 amostras se necessário.
- As médias da tabela usam apenas as últimas cinco amostras de cada nível, após a rampa inicial.
- Critério de estabilidade: amplitude dos RPM nas últimas cinco amostras menor ou igual ao maior entre 100 RPM e 5% da média, em ambas as fans.
- Estabilidade de RPM não significa equilíbrio térmico. Este ensaio não mede ganho de temperatura nem desempenho.
- A curva é uma referência empírica desta execução: firmware, temperatura e carga podem alterar os RPM para o mesmo boost.
- Boost 0 não significa fan desligada. Boost 100 é o maior pedido nesta faixa, não uma prova independente do máximo físico da fan.

## Restauração e artefatos

Perfil original: 160. Perfil final: 160.
Boosts originais: fan 50=0, fan 51=0.
Boosts finais: fan 50=0, fan 51=0.
Restauração confirmada por leitura, conforme registro bruto.

- Execução: `tools/calibrate-fans.ps1`.
- Dados brutos: `.scratch/fan-controller/fan-calibration.json`.
- Gerar novamente este relatório: `tools/summarize-calibration.ps1`.
- A primeira tentativa parou no cálculo estatístico do nível 0; restaurou o estado original. Dados preservados em `fan-calibration-attempt1.json`.
- A correção estatística foi verificada por replay das amostras como hashtables PowerShell antes de repetir o ensaio.
