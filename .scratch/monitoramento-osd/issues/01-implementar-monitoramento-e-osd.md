# Implementar monitoramento e OSD

Status: ready-for-human

Implementar as decisões aceitas em ../spec.md: telemetria auxiliar isolada, integração RTSS com slot próprio, configurações persistidas e painel escuro redimensionável. Preservar controle térmico, restauração, coleta histórica e banco existente.

## Validação

- Testes de disponibilidade/expiração, seleção de métricas, persistência e limites do protocolo RTSS.
- Compilação da GUI e regressão do controle e histórico.
- Conferência visual e validação no Euro Truck Simulator 2, documentando qualquer parte que exija interação humana.

## Comments

- 2026-10-28: implementação autorizada após entrevista Q1–Q16; trabalho iniciado.
- 2026-10-28: implementação e testes concluídos; instalador 0.5.0 gerado. OSD observado no menu do Euro Truck com dados reais de CPU/GPU e frames. Próxima ação humana: instalar com elevação e conferir CPU/RPM no aplicativo novo e em viagem; detalhes em ../validation.md. Versão instalada/sessão de fans preservadas.
