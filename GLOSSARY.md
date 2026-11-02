# Controle de ventilação

Vocabulário do controle térmico deste computador.

## Linguagem

**Fan**:
Uma das ventoinhas internas responsáveis pela circulação de ar no computador.
_Avoid_: Cooler, quando usado para designar apenas a ventoinha.

**Boost de fan**:
Ajuste solicitado para aumentar a ventilação em relação ao comportamento térmico automático. Não representa uma rotação fixa nem necessariamente uma porcentagem da rotação máxima.
_Avoid_: RPM alvo, porcentagem de velocidade.

**Rotação**:
Velocidade de giro medida de uma fan, expressa em rotações por minuto (RPM).
_Avoid_: Boost, intensidade solicitada.

**Sessão de controle**:
Período em que ajustes de boost escolhidos pelo usuário estão sob responsabilidade do programa. O estado anterior é a referência para a restauração ao encerrar a sessão.

**Perfil automático**:
Conjunto de curvas que determina o boost de cada fan a partir da temperatura associada a ela.

**Curva de ventilação**:
Relação entre temperatura e boost solicitado. Entre seus pontos, o boost varia gradualmente.

**Amostra de histórico**:
Registro pontual das temperaturas de CPU e GPU, das rotações e dos boosts de suas fans em um instante identificado.

**Histórico térmico**:
Conjunto de amostras que permite acompanhar a evolução das temperaturas, rotações e boosts ao longo do tempo.

**Métrica de monitoramento**:
Grandeza acompanhada para observar o estado do computador ou o desempenho de um jogo, como temperatura, uso, rotação ou taxa de quadros. Sua disponibilidade depende da fonte de leitura.

**Exibição na tela (OSD)**:
Apresentação das métricas selecionadas sobre a imagem de um jogo, permitindo acompanhá-las sem abrir a janela do FanController.

**Tempo de quadro**:
Tempo associado à produção ou apresentação de um quadro do jogo, expresso em milissegundos. A interpretação da medida depende da fonte de leitura.

**Taxa de quadros (FPS)**:
Quantidade de quadros por segundo observada no jogo. Seu valor depende do período e do evento usados na medição.

**Faixa de monitoramento**:
Forma compacta da exibição na tela, organizada em uma única linha com as métricas selecionadas.
