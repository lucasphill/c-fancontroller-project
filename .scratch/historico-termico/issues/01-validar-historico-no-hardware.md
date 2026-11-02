# Validar histórico térmico no hardware

Status: ready-for-human

A implementação foi autorizada após a entrevista e está descrita em ../spec.md.

## Validação interativa

1. Abrir a versão 0.4.0, acessar Histórico e conferir a primeira amostra e suas fontes.
2. Manter na bandeja por pelo menos cinco minutos e verificar a próxima amostra.
3. Conferir as três métricas ao passar o mouse e alternar os cinco períodos.
4. Encerrar e reabrir; confirmar a persistência das amostras anteriores.
5. Suspender e retomar o computador; conferir a ausência de preenchimento retroativo.
6. Verificar que a fonte GPU acompanha a seleção do aplicativo e que mudanças não unem a linha de temperatura entre fontes diferentes.

## Comments

- Testes automatizados de controle existentes e de histórico/worker passaram sem acesso ao hardware ou ao banco pessoal.
- Gráficos de 24 horas e 90 dias foram renderizados com dados simulados e inspecionados em imagem.
- A abertura interativa da prévia pela ferramenta de computer-use expirou aguardando aprovação do aplicativo; por isso a validação interativa real permanece pendente.
