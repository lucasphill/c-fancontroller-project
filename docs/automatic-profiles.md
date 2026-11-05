# Perfis automáticos

> A interface atual da aeri e as mudanças de histórico/curvas estão documentadas em [aeri](aeri.md). As descrições de abas e intervalos abaixo registram a versão anterior.

Implementados em 25/10/2026 na interface gráfica. O CLI mantém os comandos atuais.

## Uso

1. Abra `build\FanController.exe`. A abertura apenas monitora.
2. Na aba Controle, selecione Manual, Normal ou Performance e clique em Aplicar.
3. Acompanhe o modo ativo, RPM, temperatura e boost atual. Alterar o seletor
   sozinho não troca o controle ativo; o texto de estado mostra o modo em execução.
4. Restaurar encerra o perfil e verifica a restauração do perfil Dell e dos
   dois boosts anteriores. X/minimizar mantém o perfil na bandeja.

Normal e Performance usam CPU provável pelo sensor Dell 1 e GPU pela NVML.
NVML pode interferir no repouso da GPU; é obrigatória nesses dois modos.
Ao restaurar, volta a opção NVML que estava selecionada antes da ativação.
Máximo (100) seleciona Manual e solicita boost 100 nas duas fans.

## Curvas editáveis

Na aba Curvas, escolha Normal ou Performance. Cada perfil tem cinco pontos por
fan: temperatura em °C e boost solicitado. Clique em Salvar e aplicar para
persistir os dois perfis, incluindo as alterações do perfil exibido. Ao mudar o
perfil de edição com alterações não salvas, a interface pede confirmação para
descartá-las. Editar um campo não muda o controle ativo.

Temperaturas devem estar entre 1 e 125 °C, em ordem estritamente crescente.
Boosts devem estar entre 20 e 100 no Normal e entre 30 e 100 no Performance e nunca diminuir com a temperatura.
Esses limites de entrada não são limites térmicos recomendados do hardware.
O programa interpola linearmente e arredonda o boost para cima; fora da curva
mantém o valor do primeiro/último ponto. Os pontos iniciais são:

| Boost | Normal CPU | Normal GPU | Performance CPU | Performance GPU |
|---|---|---|---|---|
| 20 / 30 | 50 °C (20) | 40 °C (20) | 40 °C (30) | 30 °C (30) |
| 45 | 60 °C | 50 °C | 50 °C | 40 °C |
| 65 | 70 °C | 60 °C | 60 °C | 50 °C |
| 85 | 80 °C | 70 °C | 70 °C | 60 °C |
| 100 | 90 °C | 80 °C | 80 °C | 70 °C |

A subida ocorre na próxima leitura, sem espera intencional. A descida exige
10 segundos contínuos com temperatura pelo menos 3 °C abaixo da referência do
boost aplicado, calculada pela curva interpolada. Sair dessa condição reinicia
a espera. O ciclo normal tem espera de dois segundos mais o tempo das consultas.

Salvar uma edição do perfil ativo reinicia a avaliação da curva; pode mudar o
boost na próxima leitura. Não cria outra sessão nem substitui o estado original
que será restaurado. O histórico de 0 RPM é preservado ao editar.

As curvas ficam em `fanctl-curves.txt` ao lado do executável, com escrita em
arquivo temporário, flush e substituição do arquivo anterior. Elas são carregadas
ao abrir, mas nenhum modo é ativado automaticamente. Se o arquivo existente
estiver inválido ou ilegível, a interface informa erro e exige revisar/salvar
as curvas antes de ativar um perfil automático.

## Falhas e limites

- O piso é boost solicitado 20 no Normal e 30 no Performance, sem garantia de RPM fixos ou equivalência à curva
  automática Dell. O perfil Dell anterior pode permitir fans paradas ao restaurar.
- Temperatura obrigatória ausente, erro NVML, leitura fora de 1..125 °C ou mudança
  externa de perfil/boost interrompe o controle e tenta restaurar.
- Uma fan indicando 0 RPM continuamente por 10 segundos durante controle
  automático também provoca interrupção e tentativa de restauração.
- Valores plausíveis incorretos e sensores travados não são detectados em geral.
  O sensor Dell de CPU continua identificado como provável.
- A restauração só remove o registro da sessão após confirmação. Uma falha mantém
  o registro e a opção Tentar restaurar.

## Validação

Compilação GUI/CLI com `/W4 /WX` e testes sem hardware em `tools/test.ps1`.
Os testes cobrem interpolação, limites, persistência serializada, histerese,
contagem e recuperação de 0 RPM, fonte/temperaturas obrigatórias, atualização
dinâmica sem perder o estado original, alteração externa, falha parcial de escrita
e cancelamento entre fans.

A validação visual da nova aba e a execução dos perfis no hardware ainda estão
pendentes. O ganho de temperatura média precisa de ensaio comparável sob carga.


Atualização 0.4.1: primeiro ponto do Normal reduzido para 20 em ambas as fans,
por solicitação do usuário. Demais temperaturas e boosts preservados.
