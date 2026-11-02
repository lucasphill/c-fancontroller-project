# OSD independente do RTSS

Status: ready-for-agent

## Pedido

Investigar com REA a arquitetura do RTSS para avaliar uma exibição na tela integrada ao FanController, sem exigir um segundo programa. Conduzir entrevista com grilling e domain-modeling, uma pergunta por vez, registrando termos e decisões conforme forem aceitos.

## Contexto

A versão 0.5.0 publica métricas em slot próprio da memória compartilhada do RTSS. O ADR 0002 escolheu RTSS como renderizador externo; removê-lo exige uma decisão que substitua ou limite esse ADR. Nenhuma mudança desse tipo foi aceita ainda.

A investigação anterior dos exports de RTSSHooks64.dll consta em ../monitoramento-osd/rtss-hooks-exports.json. Ela estabelece exports declarados, não que a DLL funcione independentemente do RTSS.exe. A investigação inicial e os SDKs estão referenciados em ../monitoramento-osd/rea-investigation.md.

## Decisões aceitas

1. Oferecer apenas o FanController como aplicativo para instalar e abrir, sem exigir instalação ou abertura separada do RTSS. Componentes próprios, como DLLs, podem acompanhar o instalador e ser gerenciados automaticamente. Não há exigência de um único arquivo EXE ou processo.
2. Priorizar Euro Truck Simulator 2 em modo janela ou janela sem bordas para a primeira entrega independente. Suporte a tela cheia exclusiva fica para uma etapa posterior, com compatibilidade ainda a comprovar.
3. Mostrar o OSD somente enquanto o Euro Truck estiver em primeiro plano. Ocultar automaticamente quando o jogo for minimizado ou perder o foco, incluindo Alt+Tab, sem cobrir outros aplicativos ou o desktop.
4. O OSD deve ser transparente à interação: não receber cliques nem capturar teclado; manter os comandos direcionados ao jogo. Ajustar posição e métricas pela janela do FanController.
5. Solicitação adicional do usuário: apresentar todos os dados selecionados em uma única linha compacta na parte superior, com aparência simples, sutil e direta, tendo os overlays de Overwatch/Steam como referência de intenção visual. Esse formato substitui a organização em linhas separadas da versão dependente do RTSS; não foi solicitada reprodução exata dessas interfaces.
6. Centralizar a linha no topo, com pequena margem da borda, texto pequeno e fundo escuro translúcido. Exemplo ilustrativo aprovado: CPU 65°C · 32% | GPU 58°C · 76% | Fans 3200/3000 RPM | 120 FPS · 8,3 ms. Os números do exemplo não são leituras reais.
7. Usar o símbolo — para métricas indisponíveis na faixa compacta e apresentar o motivo detalhado no painel do FanController. Não substituir leituras ausentes por zero nem apresentar valores antigos como atuais.
8. Priorizar sobreposição externa, sem modificar arquivos do Euro Truck nem carregar código no processo do jogo. Investigar coleta de FPS e tempo de quadro por eventos gráficos do Windows; a fonte ainda requer validação local. Essa abordagem restringe o suporte inicial aos modos janela/sem bordas já aceitos.
9. Usuário aprovou o visual da faixa como base para integração ao FanController após a validação do protótipo. Integração iniciada com renderizador próprio e biblioteca de frames vinculada ao aplicativo.

## Próxima decisão

Revisar o resultado do protótipo e decidir sobre integração ao produto. O protótipo sinaliza truncamento em janela estreita; o comportamento definitivo para falta de espaço ainda deve ser fechado. A faixa e a fonte independente de frames responderam no menu do Euro Truck com RTSS fechado; ver prototype-validation.md para limites e evidências.

## Comments

- 2026-10-28: usuário aprovou construir e validar primeiro um protótipo isolado com faixa compacta e medição independente de frames, preservando a versão RTSS instalada até comprovar funcionamento. Protótipo em prototype/, build em tools/build-overlay-prototype.ps1.

- 2026-10-28: iniciada avaliação de viabilidade. Sem alteração no OSD existente, sem incorporar binários RTSS ou iniciar instrumentação de jogos.
- 2026-10-28: usuário aceitou componentes próprios gerenciados automaticamente com um único aplicativo para instalar e abrir.
