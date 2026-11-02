# Histórico térmico

Status: ready-for-human

## Decisões confirmadas

- Coletar enquanto o aplicativo estiver aberto, inclusive na bandeja, independentemente da aba ou modo de controle.
- Registrar uma leitura pontual ao iniciar e a cada cinco minutos; não agregar médias.
- Aplicativo encerrado e computador suspenso deixam lacunas no histórico.
- Reter os últimos 90 dias e remover automaticamente os registros mais antigos.
- Adicionar uma aba de visualização com dois gráficos: CPU e GPU.
- Cada gráfico contém linhas de temperatura, boost de fan e rotação, com escalas identificadas e valores reais.
- Mostrar os três valores ao passar o mouse; manter as mesmas cores por métrica nos dois gráficos.
- Conduzir a entrevista com uma pergunta por vez.
- Persistir o histórico em SQLite embutido, sem serviço de banco separado.
- Salvar o banco por usuário em %LOCALAPPDATA%\FanController\history.db, separado do executável e preservado nas atualizações.
- A temperatura da GPU acompanha a fonte efetivamente selecionada no aplicativo (Dell ou NVIDIA/NVML); cada amostra registra a fonte utilizada, sem forçar NVML para o histórico.
- A linha de temperatura GPU conecta leituras válidas adjacentes; mudanças entre Dell e NVML são indicadas por trechos tracejados e pela fonte no detalhe da amostra (correção 0.4.2).
- O boost histórico é o valor lido do firmware no instante da coleta, consistente com o monitoramento atual; não é o último valor solicitado pelo controle.
- Métricas indisponíveis são registradas como ausentes e deixam lacunas nas respectivas linhas; preservar as demais leituras disponíveis, sem preencher ausências com zero ou com o último valor.
- A aba abre nas últimas 24 horas e oferece períodos de 1 hora, 24 horas, 7 dias, 30 dias e 90 dias; a seleção de período é compartilhada pelos gráficos de CPU e GPU.
- Somente o aplicativo gráfico grava o histórico, inclusive na bandeja; comandos da CLI não gravam amostras.
- A temperatura da CPU usa o mesmo sensor do monitoramento atual, mantendo a identificação “CPU provável” no histórico, pois a associação ainda não foi confirmada definitivamente.
- Falhas de persistência não interrompem o controle das fans. Exibir aviso persistente na aba de histórico e tentar gravar novamente na próxima coleta; amostras não salvas deixam lacunas, sem promessa de recuperação.

## Confirmação final

O usuário confirmou o entendimento consolidado e autorizou a implementação.

## Ambiente

O diretório principal passa a ser D:\Codex\FanController, por solicitação do usuário.
Os arquivos foram transferidos para D: e removidos da origem após aprovação explícita do usuário. Resta apenas a pasta vazia em C:, cuja remoção foi impedida por estar em uso por outro processo.
A implementação está concluída; resta a validação interativa com hardware real.
