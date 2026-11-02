# Correção do comando boost: perfil Custom

Implementada e validada em 2026-10-24, após comparação documentada em
`docs/profile-comparison.md` e autorização do usuário para o novo fluxo.

## Comportamento final

`fanctl boost --fan 50=30 --fan 51=30` salva o perfil e os boosts anteriores,
seleciona Custom (0), confirma o perfil e então aplica os ajustes.
Aceita iniciar em equilibrado (160) ou Custom; outros perfis são recusados
antes de escrever. O intervalo de entrada continua 0–30.

Uma troca de perfil pode redefinir ajustes das duas fans. Por isso o programa
reaplica o boost anterior de uma fan omitida após a troca, e a restauração
seleciona primeiro o perfil original e depois restaura os boosts das duas fans.
O perfil é global; preservar o valor de boost de uma fan omitida não garante
preservar seu comportamento automático sob outro perfil.

Falha ou Ctrl+C inicia restauração. Mesmo uma chamada de perfil que falhe é
tratada como possivelmente aplicada. Se uma restauração de perfil falhar, o
programa ainda tenta restaurar ambas as fans e informa o estado não confirmado.
O registro persistente só é removido após confirmar o estado original completo.

## Testes simulados

`tools/test.ps1`: 17 cenários passaram, sem acesso a hardware.
Além dos cenários anteriores, foram cobertos:

- Entrada em Custom antes dos ajustes.
- Troca de perfil que redefine os boosts, inclusive o de uma fan omitida.
- Restauração do perfil antes dos boosts, preservando valores originais diferentes de zero.
- Troca de perfil aplicada seguida de resposta perdida.
- Perfil Custom não confirmado: não aplica boost.
- Falha de restauração de perfil: não declara sucesso e ainda restaura as fans.
- Cancelamento antes da troca ou entre troca e escrita de boost.
- Sessão iniciada já em Custom.

## Hardware real

Teste do executável x64 das 21:17:49 às 21:17:57 (America/Sao_Paulo):

- Original: perfil 160 e boosts 0/0.
- Amostras do executável: perfil 0, boosts 30/30.
- Nas últimas amostras: RPM 2.678 e 2.780, ainda durante rampa inicial.
- Ctrl+C: saída normal, código 0.
- Consulta WMI independente: perfil 160 e boosts 0/0 restaurados.
- Registro temporário removido; restauração de emergência externa não necessária.

Artefatos em `.scratch/fan-controller/`: `custom-boost-cli-check.json`,
`custom-boost-cli-output.txt`, `custom-boost-cli-metrics.json` e
`run-custom-boost-check.ps1`.

Compilação passou com `/W4 /WX /O2 /MT`. Executável: 325.632 bytes (318 KiB).

Este teste valida integração e restauração normal, não ganho térmico sob carga
padronizada. Falhas de firmware reais e encerramento abrupto não foram provocados.
O fechamento da janela, término forçado e perda de energia continuam sem garantia
de restauração; recuperação automática do registro ainda não está implementada.
