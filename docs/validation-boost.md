# Teste temporário de boost

Executado em 2026-10-24, das 20:50:45 às 20:51:18 (America/Sao_Paulo),
no Dell G15 5530. Teste de escrita temporária autorizado pelo usuário.

## Procedimento

Script: `tools/test-boost.ps1`.
Registro completo: `.scratch/fan-controller/boost-test.json`.

- Verificou modelo, topologia e ausência dos processos de controle conhecidos.
  Essa busca por nomes não prova ausência de qualquer controlador possível.
- Leu e gravou em disco perfil, boosts, RPM e temperaturas antes de escrever.
- Manteve o perfil 160 (0xA0), sem enviar comandos de troca de perfil.
- Solicitou boost 30 em uma fan por vez; a outra permaneceu no boost anterior.
- Confirmou o boost por leitura e coletou seis amostras com intervalo de 2 segundos.
- Restaurou o boost original antes de testar a outra fan e verificou o retorno por leitura.
- Ao final, verificou novamente perfil e boosts de ambas as fans.

## Resultado

| Fan | RPM antes da aplicação | Boost solicitado/lido | RPM nas seis amostras | Média de RPM |
| --- | ---: | ---: | ---: | ---: |
| 50 | 1.525 | 30 / 30 | 1.746–2.427 | 2.218,3 |
| 51 | 1.493 | 30 / 30 | 1.856–2.646 | 2.373,5 |

As quatro chamadas de controle (duas aplicações e duas restaurações) retornaram 0.
As restaurações de ambas as fans foram confirmadas por leitura.

Estado inicial: perfil 160, boost 0 em ambas.
Estado final: perfil 160, boost 0 em ambas.

Os RPM finais foram 1.354 e 2.454. Restaurar os ajustes não significa restaurar
instantaneamente a rotação anterior: há inércia e o controle automático continua atuando.

## Conclusões e limites

- O firmware aceita e devolve o valor de boost no perfil 160. Isso nao comprova
  que o perfil equilibrado aplique o ajuste as fans: essa conclusao anterior
  foi revista apos o relato de ausencia de efeito (ver diagnosis.md em .scratch/fan-controller).
- A amostra mostrou aumento de RPM durante o teste, mas nao houve comparacao
  pareada sob carga estavel para atribui-lo ao boost, em vez do controle automatico.
- O caminho de aplicação e restauração normal foi validado em PowerShell/WMI.
  O executável C++ continua apenas com status/watch; o comando boost ainda não foi implementado.
- Somente os valores 0 e 30 foram escritos neste experimento. Não houve teste de faixa completa.
- A codificação do protocolo foi conferida na documentação primária:
  https://docs.kernel.org/wmi/devices/alienware-wmi.html
- Não foi medida melhora de temperatura sob carga controlada, nem validada a
  prevalência de proteções do firmware durante condições extremas.
- O sensor 6 apresentou leituras de 30 e 56–57 graus ao longo da sessão.
  Sua identidade e estabilidade precisam ser investigadas antes de usá-lo em curvas.
- Falhas parciais, perda do provedor, suspensão e encerramento abrupto não foram
  provocados neste teste. A restauração observada foi no caminho normal.
