# Integrar faixa independente ao FanController

Status: ready-for-human

Promover renderização e medição de frames do protótipo para módulos de produção, preservando as decisões aceitas em ../spec.md, a seleção de métricas anterior e as proteções do controle térmico.

## Critérios

- Um aplicativo, sem dependência de RTSS ou serviço PresentMon.
- Faixa compacta centralizada, transparente à interação e vinculada ao foco do Euro Truck.
- Coleta ETW assíncrona, filtrada por PID, com expiração e recuperação após perdas.
- Preferências anteriores preservadas e migradas para o formato da faixa.
- Regressão de controle/histórico e novos testes aprovados; build e instalador com avisos da biblioteca.
- Validação real separada da sessão de fans instalada, documentando os limites restantes.

## Comments

- 2026-10-28: usuário aprovou a faixa; integração e verificação iniciadas.
- 2026-10-28: integração 0.6.0 compilada e empacotada; regressão e novos testes aprovados. Prévia visual conferida; host com os mesmos módulos de produção mostrou sensores e frames reais no Euro Truck e ocultação na troca de foco, sem RTSS. Próxima ação humana: instalar a versão e validar durante uso real/em viagem; detalhes em ../integration-validation.md.
