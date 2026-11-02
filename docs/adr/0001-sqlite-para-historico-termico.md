# SQLite para o histórico térmico

O histórico térmico usa SQLite embutido para persistir amostras locais com consultas por período e retenção de 90 dias, sem exigir um serviço de banco separado. CSV foi considerado, mas exigiria lógica própria de consulta, retenção e recuperação; um banco remoto acrescentaria infraestrutura sem uma necessidade de acesso compartilhado. A escolha introduz uma dependência de compilação e um formato persistente cujo esquema precisará evoluir preservando os dados existentes.

O arquivo fica em %LOCALAPPDATA%\FanController\history.db, por usuário e separado do executável, para preservar o histórico nas atualizações sem depender de escrita na pasta de instalação.
