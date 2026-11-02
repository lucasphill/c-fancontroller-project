# Instalador e retomada no login

- Pedido: instalável leve, inicialização na bandeja sem terminal e último perfil ativo.
- Primeiro perfil escolhido explicitamente: Normal.
- Instalador Inno Setup 0.3.0, caminho protegido em Program Files, tarefa elevada
  no login do instalador, token interativo, atraso 10s e sem limite de duração.
- Salvar último modo somente após aplicação confirmada. Preservar escolha após
  Restaurar/Sair para o próximo login. Abertura manual continua monitorando.
- Tentar restaurar ao encerrar sessão; recuperar registro válido sob trava no
  próximo --startup antes de iniciar novo controle. Não ignorar registros inválidos.
- Testes sem hardware passaram; instalação concluiu e iniciou pelo Agendador.
- Conferência de tarefa/hardware passou: tarefa elevada interativa, uma instância
  oculta, Normal ativo em Custom e quatro amostras com boosts/RPM válidos.
  Resultado em ../fan-controller/installation-check.json. Login após reinicialização real
  fica como validação humana, sem reiniciar o computador automaticamente.
