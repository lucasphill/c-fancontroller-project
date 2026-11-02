# Instalação e início automático

Versão 0.4.2, preparada em 25/10/2026 para este Dell G15 5530 e a conta Lucas.

## Instalar

Execute `dist\FanController-Setup-0.4.2-x64.exe`. O instalador solicita
administrador, instala em `C:\Program Files\FanController`, cria atalhos e uma
entrada de desinstalação no Windows. Encerre uma versão aberta usando Sair antes
de instalar/atualizar; o instalador verifica a trava e não força seu encerramento.

O pacote inicial usa Normal e inclui as curvas salvas na versão de desenvolvimento.
Atualizações preservam os arquivos de curvas e do último modo já instalados.
O histórico térmico fica por usuário em `%LOCALAPPDATA%\FanController\history.db`
e também é preservado em atualizações e na desinstalação.
O instalador inicia a tarefa ao concluir. Não precisa reiniciar para usar agora.

## Início automático

A tarefa `FanController - Lucas` executa o aplicativo com `--startup`, na sessão
interativa da conta que instalou, com privilégios elevados. O gatilho é o login
nessa conta, com atraso de 10 segundos. Não há senha armazenada pelo programa.
O consentimento de administrador é dado na instalação; não há UAC a cada login.

O programa aparece na área de notificação perto do relógio, eventualmente dentro
da seta de ícones ocultos. Não abre janela nem terminal quando o início tem sucesso.
A tarefa permite uso na bateria, não interrompe ao tirar o carregador, não tem
limite de duração e ignora tentativas de execução duplicadas.

Isso inicia depois de entrar na conta do Windows. Não controla as fans antes
do login e não é um serviço de sistema.

## Último modo

Cada Aplicar/Máximo bem-sucedido salva modo, boosts manuais e escolha NVML em
`fanctl-startup.txt`, junto ao executável instalado. Apenas selecionar um modo
não o salva. No próximo login, `--startup` ativa esse modo, usando as curvas
salvas em `fanctl-curves.txt`.

Restaurar e Sair devolvem o controle anterior nesta sessão; o último modo aplicado
permanece salvo para o próximo login. Abrir pelo atalho sem `--startup` permite
monitorar sem ativar automaticamente um perfil. Se uma instância já está rodando,
o atalho traz sua janela.

Para alterar o perfil do próximo login, abra a janela, selecione o modo e clique
em Aplicar. Para desativar o início automático, desabilite a tarefa no Agendador
de Tarefas ou desinstale o programa.

## Interrupções e falhas

Ao receber a consulta de encerramento de sessão do Windows, o programa solicita
restauração e aguarda até quatro segundos. Encerramento forçado continua sem
garantia de restauração imediata.

No início `--startup`, se existir registro de uma sessão interrompida, o programa
adquire a trava global, valida o registro, tenta restaurar o estado original e
confirma por leitura antes de remover o registro e iniciar outro controle. Se
isso falhar, preserva o registro e informa o erro; não ignora a recuperação.

A inicialização aguarda a interface Dell quando indisponível e faz tentativas
limitadas de ativação, com pausas de cinco segundos, para tolerar a preparação
dos drivers após login. Falhas persistentes mostram a janela com erro.
Arquivos de configuração inválidos também impedem a ativação automática.

## Desinstalar

Use Aplicativos instalados no Windows ou o atalho Desinstalar FanController.
Encerre primeiro o programa por Sair, permitindo restaurar as fans. A
desinstalação remove a tarefa, os executáveis e atalhos. Curvas e último modo
são preservados para eventual reinstalação.

## Construir e verificar

`tools/build-installer.ps1` usa Inno Setup portátil em `.tools/inno`, compila a GUI
em `build/package` sem substituir um executável em uso e gera o pacote em `dist`.
O compilador foi obtido da distribuição oficial 6.7.3 e sua assinatura Pyrsys B.V.
foi verificada. O instalador local do FanController não tem certificado de assinatura.

`tools/test.ps1` cobre configuração de início e recuperação, além das curvas e
sessões de controle. `tools/verify-installation.ps1`, elevado, confere a tarefa,
instância instalada, hash do binário e amostras WMI sem ajustar as fans.

Referências: [contexto de segurança das tarefas](https://learn.microsoft.com/en-us/windows/win32/taskschd/security-contexts-for-running-tasks),
[gatilho no login](https://learn.microsoft.com/en-us/windows/win32/taskschd/starting-an-executable-when-a-user-logs-on),
[Inno Setup](https://jrsoftware.org/isdl.php).

## Resultado nesta máquina

Instalação e verificação concluídas em 25/10/2026. Registro local:
`.scratch/fan-controller/installation-check.json`.

- Executável instalado igual ao pacote, confirmado por SHA-256.
- Tarefa Running, usuário Lucas, Highest/Interactive, `--startup`, atraso PT10S
  e tempo de execução PT0S (ilimitado).
- Uma instância, janela oculta e segundo disparo ignorado sem duplicação.
- Normal salvo e ativo em Custom; quatro amostras WMI confirmaram boost
  38–39 na fan 50 e 35 na fan 51, com RPM aproximadamente 3.276–3.492 e
  3.234–3.271. Amostras pontuais, sem ensaio de ganho térmico.
- Testes de configuração, recuperação, curvas e controle compartilhado passaram.
- O disparo pelo Agendador foi validado sem reiniciar. O gatilho após um login
  real permanece para conferência humana quando conveniente.

O primeiro diagnóstico sofreu expansão excessiva de metadados ao serializar
strings retornadas por Get-Content no Windows PowerShell 5.1. Os processos de
diagnóstico foram encerrados; usar File.ReadAllText corrigiu a geração do
relatório. Isso não ocorreu no controlador C++.
