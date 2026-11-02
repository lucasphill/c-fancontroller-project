# Ausência de efeito perceptível do boost

## Relato

Usuário aplicou o comando e não percebeu diferença de temperatura nem de ruído.

## Observação independente, somente leitura — 2026-10-24

Sonda: `tools/observe-fans.ps1`, executada elevada.
Resultado: `fan-observation.json`. Nenhum comando de controle foi enviado.

- Sessão do usuário ativa: `fanctl.exe boost --fan 50=30 --fan 51=30`.
- Oito amostras com espera de 2 segundos entre elas.
- Perfil 160 e boost 30 nas duas fans.
- Fan 50: 1.446–1.542 RPM, média 1.495,375.
- Fan 51: 1.457–1.550 RPM, média 1.497,125.
- Máximo reportado pelo firmware: 4.800 RPM para ambas; não calibrado fisicamente.
- Nenhum outro processo de controle conhecido encontrado por busca de nomes.
- Consulta de porcentagem retorna 0xfffffffe, fora de 0–100, não interpretada como porcentagem válida.

Isso confirma que o pedido está registrado e que as fans não estão próximas
do máximo reportado. Não confirma ausência de efeito frente a boost 0, porque
a observação não alterou a sessão ativa do usuário nem mediu uma baseline pareada.
Ruído não foi medido com instrumento.

## Hipóteses, por prioridade de investigação

1. Dependência de perfil: o equilibrado pode aceitar o registro, mas não aplicar
   o ajuste como o perfil Custom. Previsão: sob condições semelhantes, manter
   boost 30 e mudar apenas o perfil para Custom altera os RPM sustentados.
2. Intensidade pequena: 30 pode corresponder a uma solicitação modesta, perto
   da ventilação que o automático já forneceria. Previsão: após confirmar o modo
   efetivo, um aumento calibrado do valor produz aumento sustentado dos RPM.
3. Ausência de comparação térmica controlada/sensor inadequado: carga/potência
   variam e sensor 6 tem leituras intermitentes de 30 graus. Previsão: medições
   identificadas e pareadas sob mesma carga distinguem efeito térmico de ruído de medição.

## Evidência primária

- tcc-g15 GUI: habilita ajustes e envia velocidades apenas em Custom; troca
  explicitamente o perfil antes de aplicar velocidades:
  https://github.com/AlexIII/tcc-g15/blob/master/src/GUI/AppGUI.py
- Wrapper: Custom=0; USTT Balanced=160:
  https://github.com/AlexIII/tcc-g15/blob/master/src/Backend/AWCCWmiWrapper.py
- Dell explica que offset e velocidade real não têm correspondência direta:
  https://www.dell.com/support/kbdoc/en-us/000221917/fan-speed-offset-configuration-on-performance-page-in-alienware-command-center

## Correção da conclusão anterior

O readback confirmou transporte, gravação e restauração dos valores. Os testes
curtos anteriores não estabeleceram causalidade para aumento de RPM, nem provaram
que perfil 160 é adequado ao controle manual. Não há correção de código aplicada
enquanto a hipótese de perfil não for testada.

## Próximo experimento proposto, ainda não executado

- Primeiro encerrar a sessão atual com Ctrl+C, preservando seu fluxo de restauração.
- Capturar e persistir perfil e boosts originais e confirmar ausência de sessão concorrente.
- Comparar boost 30 em perfil 160 e boost 30 em Custom (0), cerca de 30 segundos
  por condição, sob a mesma atividade; mudar apenas o perfil entre as condições.
- Verificar perfil e boosts por leitura, acompanhar RPM e temperaturas e interromper em falha.
- Restaurar o perfil e os boosts originais ao final, verificando por leitura.
- Não aumentar para 60/100 nesta comparação. A troca temporária de perfil ainda
  depende da decisão do usuário; nenhum teste de troca foi executado.

## Resultado do experimento autorizado

Usuário autorizou a comparação e confirmou encerramento da sessão anterior.
Experimento concluído; perfil original 160 e boosts 0/0 restaurados por leitura.
Custom com boost 30 apresentou RPM sustentados maiores que equilibrado com o
mesmo boost. Últimas cinco amostras: fan 50, 1.904 contra 2.969,8 RPM; fan 51,
1.983,4 contra 3.151 RPM. Ver docs/profile-comparison.md.

A hipótese de dependência de perfil ganhou evidência experimental. O usuário
autorizou a correção do executável para entrar em Custom e restaurar perfil e
boosts ao sair. Correção implementada, com 17 cenários simulados e Ctrl+C no
hardware validados; detalhes em docs/validation-custom-cli.md.
Ainda não foi estabelecida meta de ganho térmico sob carga padronizada.
Não afirmar que equilibrado sempre ignora boost ou que a diferença de temperatura
medida foi causada somente pela ventilação.
