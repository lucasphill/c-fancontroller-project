# Validar faixa e frames sem RTSS

Status: ready-for-human

Protótipo isolado aprovado pelo usuário após entrevista de escopo. Código em ../prototype/, executável em build/overlay-prototype/FanController-Overlay-Prototype.exe e execução por tools/run-overlay-prototype.ps1 -Elevated.

## Pergunta

Uma faixa externa de uma linha e coleta ETW vinculada estaticamente permitem exibir sensores e frames no Euro Truck sem RTSS ou código no jogo?

## Resultado

Sim, na configuração e no menu observados: faixa centralizada no topo, sensores Dell/NVIDIA reais, FPS e intervalo médio entre apresentações por PresentData. RTSS estava fechado. A versão instalada foi preservada. Evidências, custo e limites em ../prototype-validation.md.

## Próxima decisão humana

Avaliar o visual e autorizar a integração no FanController. Validar modos janela/sem bordas explicitamente, interação durante uma viagem e comportamento em janelas estreitas antes de substituir a versão estável.

## Comments

- 2026-10-28: compilação e prova real concluídas. Fontes experimentais preservadas na pasta prototype/, com versão upstream fixada e avisos de licença; código não incorporado ao produto.
