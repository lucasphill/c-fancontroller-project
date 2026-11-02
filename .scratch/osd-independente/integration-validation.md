# Validação da integração 0.6.0

Data: 2026-10-28.

## Implementação

- `src/compact_osd.h`: seleção de métricas, faixa única, ausências como — e leitura das preferências antigas com migração ao salvar. Ativação/máscara são preservadas; a antiga posição por canto é substituída pelo topo central aprovado.
- `src/overlay_window.h`: renderização própria, transparência à entrada, reação a foco/minimização/movimento por eventos Windows fora do processo do jogo. Recursos gráficos e callbacks são liberados pelo proprietário. Existe temporizador de verificação de vida/fallback; a atualização normal dos valores continua em um segundo.
- `src/presentmon_frames.h/.cpp`: coletor assíncrono de frames, com sessão ETW exclusiva, filtro PID, detecção de saída, expiração de três segundos e recuperação após perdas. Abertura e fechamento da captura ficam em sua thread; o ciclo térmico e a thread da interface não aguardam essas operações.
- `src/frame_metrics.h`: janela recente por swapchain, sem somar cadeias; limites de memória e invalidação/recuperação de intervalos.
- GUI atualizada sem controles RTSS, com prévia da faixa e seleção persistida; captura solicitada somente quando necessária para a faixa ou para o painel de monitoramento visível.
- Biblioteca PresentData vinculada estaticamente. Manifesto de administrador existente preservado. SQLite, curvas, ajustes, restauração e frequência histórica preservados.

## Verificação automatizada

`tools/test.ps1` aprovado, incluindo regressões de controle/boost/GPU/perfis/início/histórico/gráficos e novos testes:

- Migração das preferências de todos os cantos antigos, máscaras inválidas e roundtrip do novo formato.
- Todas as 256 seleções de métricas, unidades, ausência/zero e valores não finitos.
- Filtro por PID, troca de identidade, intervalos, seleção de swapchain, estado limitado, expiração e recuperação após perda.

Logs: `integration-tests.log`, `integration-preview-build.log`, `integration-installer-build.log` e `integration-live-build.log`. Código próprio compilado com C++17 /W4 /WX; upstream original com /W3. O wrapper define explicitamente o mesmo layout release do consumidor que a biblioteca estática.

## Conferência visual e real

- Prévia da GUI conferida: painel Monitoramento, nova aba OSD, texto de posição fixa e prévia compacta legível. Desmarcar FPS removeu somente o campo correspondente, sem gravar as preferências da sessão de prévia.
- `tests/independent_osd_live.cpp`, compilado por `tools/test-independent-osd-live.ps1`, usa os módulos de produção e consultas de hardware somente leitura. Não grava curvas, boosts, perfis, registro de restauração ou histórico e não interrompe a sessão instalada.
- Execução elevada após UAC confirmado pelo usuário. No menu do Euro Truck, foram observados sensores Dell/NVIDIA, RPM, FPS e intervalo médio; exemplo: CPU 67 °C, GPU 55 °C, fans 1.684/1.983 RPM, 240,15 FPS e 4,16 ms.
- RTSS estava fechado. Imports do executável normal somente de DLLs do Windows; NVML é resolvida dinamicamente no driver já instalado. Não há serviço PresentMon ou DLL RTSS necessária. Ver `integration-dependencies.txt`.
- Faixa reapareceu ao trazer o jogo à frente. Em Alt+Tab, a captura imediatamente seguinte mostrou o Codex sem a faixa. Isso corrige a transição breve observada no protótipo periódico.
- Os registros do host (`integration-live.jsonl`) incluem foco/visibilidade e dados reais. Um resumo intermediário (`integration-summary.json`) teve 182 amostras, 75 com FPS e zero declarando visibilidade fora de foco. O contador de uma perda acumulada coexistiu com leituras recuperadas.
- Custo do host, não do aplicativo inteiro: em 78,48 s, 2,06 s de CPU, aproximadamente 2,63% de um núcleo lógico, 44,88 MiB de working set e 22,17 MiB privados. Ver `integration-cost.json`. Isso não mede impacto no FPS, nem separa o custo de sensores/coleta/desenho.
- O host encerrou automaticamente após cinco minutos; a ausência do processo PID 11468 foi conferida. Não ficou um coletor de teste executando após a validação.

## Artefato e limites

Instalador: `dist/FanController-Setup-0.6.0-x64.exe`. Inclui avisos MIT/terceiros do PresentMon. Hashes em `integration-artifact-hashes.json`. Nenhum instalador foi executado e a versão instalada não foi substituída durante esta validação.

O host validou os módulos compartilhados e a prévia validou a interface; o novo aplicativo completo ainda deve ser instalado e conferido em uso real. A prova gráfica foi no menu/configuração atual do Euro Truck. Modos janela/sem bordas precisam de conferência explícita, assim como viagem, DPI/monitores diferentes e interação manual em pontos precisos; o automatizador de coordenadas do jogo não foi confiável para esse último ensaio. Não se promete suporte geral a tela cheia exclusiva ou a outros jogos.

A seleção de swapchain por atividade recente é uma heurística documentada e limitada a este alvo inicial. O símbolo — é preservado em falhas; não há fallback silencioso para números antigos. A versão estável pode ser mantida até a conferência humana do instalador.
