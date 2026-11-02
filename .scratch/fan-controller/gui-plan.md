# Interface gráfica e execução em segundo plano — proposta

Solicitação: interface simples e execução sem terminal aberto, mantendo baixo consumo.
Escopo confirmado, primeira implementação concluída e fluxo básico de bandeja/restauração validado pelo usuário.

## Caminho recomendado

- C++ com janela Win32 nativa e ícone na área de notificação do Windows.
- Executável gráfico próprio, compartilhando o controle térmico com o CLI existente.
- Extrair de main.cpp o acesso Dell e a responsabilidade por trava, registro,
  leitura, aplicação e restauração. A interface gráfica não deve reproduzir essa lógica.
- Executar consultas e ajustes em uma thread de trabalho que inicialize COM,
  mantendo a janela responsiva. Compartilhar resultados com a janela, sem usar
  objetos COM diretamente de outra thread.
- Preservar exclusão entre GUI e CLI durante controle ativo.
- Manter NVML opcional e desligada por padrão.

## Primeira janela proposta

Escopo confirmado pelo usuário: uma janela com RPM, temperaturas, dois controles
0–100, Aplicar, Máximo e Restaurar; opção NVML desmarcada por padrão.
Restaurar encerra o controle manual e devolve perfil e boosts anteriores.

- RPM das duas fans, temperatura CPU provável e GPU, com identificação da fonte.
- Dois ajustes de boost 0–100 e opção de ajustar ambos juntos.
- Aplicar, restaurar controle automático/estado anterior e atalho para boost 100.
- Estado visível: monitorando, controle ativo, restaurando ou erro.
- Menu da bandeja para abrir janela e sair restaurando os ajustes anteriores.

## Ciclo de vida proposto

Confirmado inicialmente: ao abrir, apenas monitorar; ajustes dependem de Aplicar
ou Máximo. Persistência de preferências e início automático ficam para etapa posterior.

Confirmado: fechar a janela no X apenas a oculta e mantém o programa na bandeja. Sair pelo
menu inicia restauração e só encerra normalmente após informar o resultado.
Execução em segundo plano se refere inicialmente à sessão do usuário conectado;
início automático com Windows e execução antes do login são decisões separadas.
Não instalar serviço ou tarefa agendada nesta etapa sem definir essa necessidade.

## Pontos ainda a resolver, uma pergunta por vez

Confirmado: se Sair não conseguir confirmar a restauração, manter janela aberta,
mostrar o erro e permitir nova tentativa. Preservar o registro dos valores anteriores.

Suspensão/retomada e recuperação automática de registro pendente continuam fora
da primeira versão. A inicialização encontra o registro pendente e recusa um novo
controle, conforme regra já existente; não reaplica ajustes silenciosamente.

## Implementação

- Executável `build/FanController.exe`, subsistema Windows, sem console.
- Manifesto solicita administrador para a interface Dell, com DPI PerMonitorV2.
- Janela única e bandeja; abrir outra instância traz a existente para frente.
- Fechar/minimizar oculta a janela quando a bandeja está disponível.
- Consultas na thread de trabalho a cada 2 segundos, com COM inicializado nela.
- CLI e GUI compartilham `windows_backend.h`, `control_session.h`, `boost_session.h`
  e os módulos de temperatura; não há subprocesso de terminal para controlar as fans.
- Ao reaplicar valores com controle ativo, restaura a sessão anterior e só então
  inicia outra sessão. Se a restauração falhar, não envia o novo pedido.
- NVML desmarcada inicialmente; mudar fonte requer encerrar o controle manual.
- Primeiro teste visual mostrou janela monitorando e depois controle ativo em
  100/100, com registro de origem perfil160/boost0/0 preservado.
- Testes: 18 cenários de boost, cenários GPU/CLI e 7 de coordenação de controle passaram.
- A automação capturou a janela, mas não acionou botões da janela elevada;
  cliques de validação foram solicitados ao usuário, sem contornar permissões Windows.
- Usuário confirmou X → reabrir na bandeja → Restaurar → Monitorando.
- Registro de sessão removido após restauração. Documentação em docs/gui.md.

## Referências

- Janela nativa: https://learn.microsoft.com/en-us/windows/win32/learnwin32/your-first-windows-program
- Ícone de bandeja: https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyiconw
