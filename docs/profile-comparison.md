# Comparação de perfis com boost 30

Executada em 2026-10-24, das 21:13:02 às 21:14:06 (America/Sao_Paulo),
no Dell G15 5530, após autorização explícita para troca temporária de perfil.

Script: `tools/compare-profiles.ps1`.
Dados completos: `.scratch/fan-controller/profile-comparison.json`.

## Procedimento

A sessão anterior do usuário foi encerrada com Ctrl+C e a ausência do processo
e do registro anterior foi conferida antes do experimento. O script adquiriu a
mesma trava global usada pelo comando boost.

Estado inicial: perfil 160, boosts 0 e 0.

Foram coletadas 15 amostras por condição, com espera de 2 segundos entre elas:

1. Perfil equilibrado 160 e boost 30 nas duas fans.
2. Perfil Custom 0 e boost 30 nas duas fans.

Nenhuma carga sintética foi iniciada; a atividade do sistema não foi controlada.
O perfil e os valores de boost foram confirmados por leitura durante o teste.
O script não enviou boost acima de 30.

## RPM

| Fan | Média das 15 amostras: equilibrado | Média das 15 amostras: Custom | Últimas 5: equilibrado | Últimas 5: Custom |
| --- | ---: | ---: | ---: | ---: |
| 50 | 1.750,1 | 2.802,3 | 1.904,0 | 2.969,8 |
| 51 | 1.812,0 | 2.974,5 | 1.983,4 | 3.151,0 |

As últimas cinco amostras reduzem a influência da rampa inicial, sem constituir
uma prova formal de equilíbrio térmico. A diferença observada é compatível com
a dependência de perfil encontrada na implementação tcc-g15:
https://github.com/AlexIII/tcc-g15/blob/master/src/GUI/AppGUI.py

## Temperatura e interpretação

O sensor 1 teve médias de 67,6 graus no equilibrado e 65,87 no Custom.
Essa diferença é exploratória: o teste foi curto, sequencial e sem carga/potência
padronizadas. Trocar perfil também pode afetar política de energia; não é possível
atribuir a diferença térmica exclusivamente ao aumento de ventilação.
O sensor 6 voltou a apresentar uma leitura de 30 graus entre leituras próximas de
54–55; sua identidade/estabilidade permanece pendente.

O resultado dá evidência forte de que selecionar Custom é relevante para obter
uma resposta sustentada maior com boost 30 nesta máquina. Não demonstra que
boost no perfil equilibrado seja sempre ignorado, nem calibra boost em RPM.

## Restauração

O script restaurou primeiro o perfil 160 e depois boost 0 nas duas fans.
Leituras finais confirmaram os três valores originais, sem erros de restauração.
RPM finais ainda elevados são esperados imediatamente após a troca; restaurar
o ajuste não exige que a rotação retorne instantaneamente ao valor inicial.

## Consequência para o programa

A comparação motivou a correção para entrar em Custom na sessão de boost e
restaurar perfil e boosts ao sair. A alteração foi posteriormente autorizada,
implementada e validada; veja `docs/validation-custom-cli.md`.
