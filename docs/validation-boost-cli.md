# Validação do comando C++ de boost

Executada neste Dell G15 5530 em 2026-10-24.

## Implementação

- `fanctl boost --fan 50=30 [--fan 51=30]`.
- Interface COM/WMI nativa, sem subprocesso PowerShell em tempo de execução.
- Intervalo inicial de entrada: inteiros de 0 a 30. Apenas os extremos foram
  escritos e testados nesta máquina; não há alegação de calibração da faixa completa.
- Mantém o perfil 160; recusa iniciar em outro perfil.
- Snapshot original completo antes de qualquer escrita, com revalidação antes de aplicar.
- Registro persistido junto ao executável, com flush antes de escrever no hardware.
- Uma trava global impede duas sessões do próprio programa ao mesmo tempo.
- Após erro ou Ctrl+C, restaura todas as fans em que tentou escrever, inclusive
  quando a chamada falhou sem permitir saber se o firmware aceitou o ajuste.
- Restauração faz até duas tentativas por fan e verifica os valores por leitura.
- Compara perfil e boosts de ambas as fans ao final. Não sobrescreve um perfil
  alterado externamente; informa que o estado anterior não foi integralmente confirmado.
- Resultado 0: saída normal com restauração confirmada; 1: falha operacional;
  2: argumentos inválidos; 3: restauração não confirmada.

## Hardware real

Executado das 20:55:14 às 20:55:23, horário de São Paulo.

- Estado inicial: perfil 160, boost 0 nas duas fans.
- Aplicação: boost 30 nas fans 50 e 51, ambas confirmadas por leitura.
- Sinal Ctrl+C enviado na console isolada do processo de teste.
- Executável informou restauração de ambas as fans e retornou 0.
- Leitura independente via WMI confirmou perfil 160 e boost 0 nas duas fans.
- Registro temporário da sessão removido após a verificação.
- A restauração independente de emergência do harness não foi necessária.

Amostra curta: 11.636.736 bytes de memória residente (cerca de 11,1 MiB),
15,625 ms de CPU do processo nos 6.500 ms antes do sinal. Esses números não
incluem o custo de processos do provedor WMI.

Executável: 323.584 bytes (316 KiB), compilado com `/W4 /WX /O2 /MT`.

Artefatos em `.scratch/fan-controller/`: `boost-cli-check.json`,
`boost-cli-output.txt`, `boost-cli-metrics.json`, `run-boost-check.ps1` e
`watch-check.cpp`. O harness tem um caminho de restauração independente caso
o executável falhe; sua utilização seria registrada no JSON.

## Falhas simuladas

`tools/test.ps1` compila e executa `tests/boost_session_test.cpp`, sem hardware:

1. Aplicação e restauração normais, mantendo a fan omitida intacta.
2. Falha na leitura original: nenhuma escrita.
3. Falha na segunda fan após o firmware simulado já aplicar o ajuste: restaura ambas.
4. Readback diferente do solicitado: interrompe e restaura.
5. Falha de sensor durante monitoramento: interrompe; restauração independe desse sensor.
6. Falha de restauração em uma fan: informa falha e ainda restaura a outra.
7. Mudança externa de perfil: detecta e não declara restauração integral.
8. Cancelamento entre as duas escritas: interrompe a aplicação e restaura a fan ajustada.
9. Mudança de boost antes de aplicar: recusa sem escrever.
10. Pedidos fora da faixa, IDs inválidos e duplicados: recusados sem escrever.

Sete casos adicionais de argumentos inválidos também foram exercitados no executável.

Os bloqueios de sessão concorrente e registro anterior existente foram exercitados
com o executável elevado: ambos recusaram com código 1, sem aplicar boost.
O registro de teste foi preservado pelo executável e removido pelo harness somente
após conferir seu conteúdo. Resultado: `boost-guards-check.json`.

## Limites

Não foi feita medição de ganho térmico sob carga controlada. A faixa completa de
boost, identidade CPU/GPU dos sensores e controle por curva continuam pendentes.
Falhas de firmware reais, suspensão/retomada e encerramento abrupto não foram
provocados no hardware. Recuperação automática do registro persistido não está
implementada; a presença do registro impede nova sessão até avaliação manual.
