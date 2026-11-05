# aeri · interface Estúdio

A atualização 0.7.0 adota o nome **aeri**, em minúsculas, e a composição Estúdio aprovada. A janela usa os mesmos estilos do protótipo: navegação lateral, duas faixas de CPU/GPU, temperatura e RPM grandes, curvas recentes e cores azul esverdeado/roxo. O símbolo de fluxo de ar tem versões vetorial, monocromática e ícone Windows com múltiplas resoluções em `assets/`.

## Telas

- **Visão geral:** temperatura e rotação atuais, fonte de leitura, modo ativo e gráficos recentes com detalhes sob o cursor. Leituras ausentes aparecem como traços.
- **Histórico:** gráficos separados de temperatura e RPM, CPU/GPU comparáveis, cursor sincronizado, filtro por componente e períodos de 15 minutos a 90 dias. Arrastar seleciona um trecho; Voltar desfaz o zoom. O trecho é consultado novamente no banco para recuperar o detalhe disponível. Eventos abrem a configuração preservada.
- **Perfis e curvas:** Normal e Performance, pontos arrastáveis ou ajustáveis pelos campos, inclusão/remoção entre 2 e 16 pontos. Aplicar salva as curvas e atualiza o perfil ativo quando suas curvas mudam. Ativar perfil é uma ação separada. O controle manual e Restaurar ficam nesta tela. Sair da tela com rascunho oferece Aplicar, Descartar e Continuar editando.
- **Configurações:** vidro opcional e seleção das métricas do OSD. FPS aparece somente durante jogos na faixa de monitoramento.

Fechar/minimizar mantém o aplicativo na bandeja. Encerrar tenta restaurar o estado anterior; uma falha mantém a aplicação e o registro disponíveis para tentar novamente.

## Tecnologia e compatibilidade

O núcleo permanece em C++. A interface local é embutida no executável e apresentada em **Microsoft Edge WebView2 Evergreen**, que precisa estar instalado. A página não busca recursos na internet. O SDK é baixado no primeiro build pelo NuGet oficial, com versão e hash fixados. Veja o [ADR 0004](adr/0004-webview2-para-interface-aeri.md).

O Acrylic do Windows permite perceber o conteúdo atrás da janela quando o sistema oferece suporte e transparência está habilitada. A alternativa é fundo sólido. O material e seu nível de desfoque são administrados pelo Windows. O OSD possui fundo independente.

`aeri.exe` é o nome público da build. O alias `FanController.exe`, pasta de instalação, tarefa de login, mutex, banco e arquivos de sessão continuam compatíveis com instalações anteriores. Não são abertas sessões paralelas pelo rebranding.

## Compilar e avaliar

```powershell
.\tools\test.ps1
.\tools\build-gui.ps1 -OutputDirectory build/aeri-release
.\tools\build-gui.ps1 -Preview -OutputDirectory build/aeri-preview
```

`build/aeri-release/aeri.exe` é a build de produção. `build/aeri-preview/aeri.exe` usa dados simulados, não inicia o worker térmico nem o OSD e não grava curvas, histórico ou preferências reais. O WebView pode criar seu próprio cache de apresentação em `%LOCALAPPDATA%\aeri-preview`.

Na bandeja, a apresentação fica oculta e suspensa; sensores, controle e histórico continuam funcionando. No login com `--startup`, o WebView é criado somente quando a janela for aberta ou precisar apresentar um erro.

## Validação desta atualização

Testes sem hardware cobrem curvas antigas e quantidade variável de pontos, migração SQLite v1, extremos nos resumos, configuração histórica imutável, retenção e isolamento da coleta. A interface foi conferida na janela Windows em modo de prévia, incluindo Visão geral, Histórico, detalhes da medição e inclusão de ponto no editor.

Uma medição pontual da prévia com a janela aberta encontrou aproximadamente **245 MiB de memória privada** e **463 MiB de working set somado** entre sete processos (host e WebView2). O working set somado pode contar páginas compartilhadas mais de uma vez; não representa memória exclusiva. A coleta real e o impacto em jogos ainda precisam de medição em uso prolongado.
