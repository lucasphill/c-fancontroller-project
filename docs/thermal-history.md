# Histórico térmico

A aba **Histórico** contém um gráfico para **CPU provável / fan 50** e outro para
**GPU / fan 51**. Cada gráfico tem três séries: temperatura em vermelho (°C), boost
lido em roxo e rotação em azul (RPM). As escalas são independentes, identificadas
com a cor da série; a altura de duas linhas não indica igualdade entre unidades.
Boost não é porcentagem da rotação máxima. A escala de boost cresce se o firmware
informar valores acima de 100, e a de RPM se adapta às amostras visíveis.

O período inicial é **24 horas**, com opções de **1 hora**, **7 dias**, **30 dias**
e **90 dias**, aplicadas aos dois gráficos. Passe o mouse sobre o gráfico para
consultar os valores de uma amostra e sua data e hora locais. A consulta usa os
registros originais; em períodos densos, o desenho conserva os extremos por coluna
de pixels. As curvas não atravessam amostras ausentes ou intervalos de coleta
perdidos. Mudanças entre Dell e NVML usam trechos tracejados na linha de
temperatura GPU, mantendo a curva visível e identificando a mudança de fonte.

## Coleta

A interface continua seu monitoramento habitual; o histórico aproveita uma leitura
pontual ao iniciar o monitoramento e a cada cinco minutos. Não são médias de cinco
minutos. Funciona na bandeja e em todos os modos, sem depender da aba aberta. A CLI
não grava histórico. Aplicativo encerrado e computador suspenso deixam lacunas;
ao retomar, não há preenchimento retroativo dos intervalos perdidos.

CPU mantém a identificação **CPU provável** e o sensor Dell atual. A GPU acompanha
a fonte selecionada pelo aplicativo, Dell ou NVIDIA/NVML, identificada em cada
amostra e no detalhe ao passar o mouse. Dell não é uma leitura GPU validada pela
NVIDIA; o histórico não força a ativação de NVML. O boost salvo é o lido do
firmware, não o último valor solicitado pelo usuário ou pela curva.

Métricas indisponíveis ficam ausentes, nunca substituídas por zero nem pelo último
valor. Leituras parciais já obtidas antes de uma falha de consulta são preservadas.
RPM igual a zero é uma leitura válida e diferente de ausência.

## Persistência

O banco SQLite é incorporado ao executável, sem DLL ou serviço adicional. O arquivo
por usuário fica em `%LOCALAPPDATA%\FanController\history.db`, fora da pasta de
instalação, e permanece após atualizar ou desinstalar o aplicativo. A tabela contém
o instante UTC, seis métricas opcionais e a fonte GPU, com índice temporal e versão
de esquema. A interface converte as datas para o fuso local.

Registros anteriores a 90 dias são removidos ao abrir o histórico e nas gravações.
Inserção e retenção são uma transação: uma falha não deixa uma gravação parcial.
Banco e consultas são processados em uma thread separada do controle térmico.
Falhas de acesso ou gravação mostram um aviso persistente na aba; a coleta seguinte
tenta novamente. Amostras não salvas são perdidas e deixam lacunas. O histórico
não interrompe o controle nem solicita alterações às fans.

## Validação

`tools/test.ps1` cobre intervalo, retomada sem preenchimento retroativo, conversão,
valores ausentes, reabertura, consultas por período, retenção e rollback após falha.
Os testes usam bancos temporários e não acessam o hardware nem o histórico pessoal.
Também verificam o worker de persistência, a seleção assíncrona de período e que
leituras frequentes não geram registros extras.

`tools/test-history-ui.ps1` compila `build/history-preview.exe`, uma prévia visual
com dados simulados. Executar com `--render` gera `history-preview-24h.png` e
`history-preview-90d.png` no diretório de trabalho, usando o mesmo desenho da GUI.
A prévia não abre o banco pessoal nem acessa o hardware.
