# Monitoramento e exibição na tela

Status: ready-for-agent

## Objetivo solicitado

Analisar MSI Afterburner e RivaTuner Statistics Server instalados neste computador com o MCP REA para orientar melhorias no FanController: mostrar leituras no canto da tela, ampliar os dados monitorados e melhorar a interface usando o MSI como referência.

## Processo

Entrevista em rodadas pelas skills grilling e domain-modeling, conforme grill-with-docs. A implementação depende da confirmação de entendimento compartilhado ao final da entrevista. A investigação inicial é somente leitura; não altera controle de fans, configurações dos aplicativos analisados nem inicia instrumentação de processos.

## Contexto confirmado

- O FanController controla boost de fans, distinto de rotação medida, conforme GLOSSARY.md.
- O histórico térmico usa SQLite local, com retenção de 90 dias, conforme ADR 0001. Ainda não há decisão sobre persistir novas métricas.

## Decisões aceitas — primeira rodada

1. Priorizar temperaturas e uso de CPU/GPU, rotações e FPS/tempo de quadro no canto do jogo, com seleção das métricas disponíveis e validação no hardware. Outras métricas ficam para uma etapa posterior.
2. Manter controle de fans e acrescentar monitoramento e exibição na tela. Overclock, tensão e limites de potência estão fora desta melhoria.
3. Criar interface própria do FanController inspirada na organização do MSI: indicadores, gráficos, seleção de métricas e perfis. Preservar a distinção entre boost solicitado e rotação medida.

## Decisões aceitas — segunda rodada

4. Usar RTSS instalado e executando para renderizar o OSD. Com RTSS fechado, manter o controle de fans e indicar OSD indisponível.
5. Obter temperaturas, uso e rotações diretamente das fontes disponíveis do computador, sem exigir MSI Afterburner aberto. Usar RTSS para FPS/tempo de quadro e exibição, conforme leituras validadas. Leituras ausentes são indisponíveis, nunca zero.
6. Novas métricas somente ao vivo nesta entrega. Preservar o histórico atual de temperaturas, rotações e boosts e a coleta a cada cinco minutos.
7. Painel escuro, compacto e redimensionável, com indicadores CPU/GPU, gráficos recentes e acesso a Controle, Curvas, Histórico e OSD. Identificar boost solicitado e RPM medido separadamente.

## Confirmação do escopo

Usuário confirmou o escopo e autorizou iniciar a implementação em 2026-10-28. Manter uma pergunta por vez se surgir uma decisão que exija alteração do escopo.

## Decisões aceitas — apresentação do OSD

8. Usar o canto superior esquerdo como posição inicial do OSD. Permitir escolher outro canto nas configurações do FanController e salvar a escolha entre reinicializações. A implementação desse posicionamento pela integração com RTSS ainda precisa ser validada.
9. Permitir marcar quais métricas aparecem no OSD e persistir a seleção entre reinicializações. Seleção inicial: temperaturas e uso de CPU/GPU, RPM das fans, FPS e tempo de quadro, conforme disponibilidade.
10. Atualizar a apresentação de temperaturas, uso e RPM no painel e no OSD uma vez por segundo, com coleta adicional separada do controle das fans. FPS e tempo de quadro seguem os dados disponíveis do RTSS. A frequência de apresentação não implica repetir leituras lentas ou bloquear o controle; a atualidade de cada leitura deverá ser considerada.
11. Incluir controles para mostrar/ocultar o OSD na janela do FanController e no menu da bandeja. Persistir a preferência entre reinicializações; na primeira instalação, iniciar com o OSD desligado.
12. Quando uma métrica deixar de atualizar, mostrar indisponível e manter as demais funcionando, sem apresentar o último valor como atual. Preservar as proteções existentes para falhas de temperaturas usadas no controle das fans. Definir o critério de expiração de leituras durante o detalhamento técnico e validá-lo com testes.
13. Usar o Euro Truck Simulator instalado neste computador como jogo de referência para validar o OSD. Verificar localmente a edição, o executável e a compatibilidade; o nome informado não estabelece esses detalhes. Validar posição, legibilidade, FPS e tempo de quadro em jogo real.
14. Organizar inicialmente o OSD em três linhas compactas: CPU (temperatura e uso), GPU (temperatura e uso) e jogo (FPS e tempo de quadro). Acrescentar uma linha de RPM das fans quando selecionada. Respeitar a seleção de métricas definida na decisão 9.
15. Quando o RTSS estiver fechado, oferecer um botão Abrir RTSS. A inicialização automática do RTSS fica sob controle do usuário; esta melhoria não a habilita por conta própria.

## Verificações de implementação

- Validar o contrato da memória compartilhada RTSS: versão, limites, exclusão de escrita, slot próprio, liberação e dados do jogo em primeiro plano.
- Validar fontes independentes para uso de CPU/GPU e expiração das leituras.
- Testar configuração persistida, seleção de métricas e falhas isoladas do controle térmico.
- Compilar com /W4 /WX e executar regressão das proteções e do histórico.
- Inspecionar visualmente painel/OSD; validar integração real no Euro Truck Simulator 2 instalado em D:\SteamLibrary\steamapps\common\Euro Truck Simulator 2.

## Comments

- Implementação 0.5.0 concluída, com instalador e regressão aprovados. Validação real parcial no Euro Truck e limites registrados em validation.md. Pendência humana de instalação/validação elevada registrada em issues/01-implementar-monitoramento-e-osd.md.

- 2026-10-28: iniciada entrevista; investigação estática dos aplicativos e leitura da arquitetura do projeto em paralelo. Nenhuma alternativa de implementação foi aceita ainda.
- 2026-10-28: usuário aceitou as três recomendações da primeira rodada. Dependências, persistência e detalhes da interface continuam pendentes.
- 2026-10-28: usuário aceitou Q4–Q7 e solicitou uma pergunta por vez. Registrada a escolha de integrar o OSD ao RTSS sem dependência de MSI Afterburner para sensores.
