# Perfis automáticos — decisões em andamento

## Confirmado pelo usuário

- Criar perfis Normal e Performance com curvas de boost por temperatura.
- Interpretar inicialmente “nunca abaixo do default” como nunca solicitar boost
  abaixo de um piso positivo validado, sem garantir equivalência ao automático Dell.
- Piso inicial: boost 30 nas duas fans, em ambos os perfis.
- Cada fan segue sua temperatura associada: fan 50 pelo sensor Dell de provável
  CPU; fan 51 pela temperatura da GPU via NVML. A identificação do sensor de CPU
  ainda não foi confirmada por uma fonte independente.
- Se uma fan permanecer indicando 0 RPM por 10 segundos com boost positivo
  solicitado, interromper o perfil, tentar restaurar o estado anterior e mostrar
  erro. Considerar o tempo contínuo de leituras zeradas, incluindo a partida.
- Durante Normal ou Performance, usar NVML para temperatura GPU. No monitoramento
  simples, NVML continua opcional. Usuário aceitou a possível interferência no
  repouso da GPU associada à ativação dos perfis automáticos.
- Se uma temperatura necessária ficar indisponível, interromper o perfil,
  tentar restaurar o estado anterior e mostrar erro. Não reutilizar leitura
  antiga como se fosse atual. O piso 30 só se aplica enquanto o perfil está ativo;
  a restauração pode devolver ao automático Dell a possibilidade de parar as fans.
- A calibração em Custom mediu aproximadamente 2.899 RPM na fan 50 e
  3.111 RPM na fan 51 nesse ponto. O piso é de boost solicitado, não garantia
  absoluta de RPM constante ou de impossibilidade de parada física.

## Interface aprovada

- Seletor Manual / Normal / Performance e botão Aplicar na janela principal.
- Restaurar devolve perfil e boosts anteriores.
- Ao abrir, apenas monitorar até ativação explícita de um modo.

## Curvas iniciais aprovadas

Interpolar linearmente entre os pontos; abaixo do primeiro ponto manter boost
30, acima do último manter boost 100.

| Boost | Normal CPU | Normal GPU | Performance CPU | Performance GPU |
|---|---|---|---|---|
| 30 | 50 °C | 40 °C | 40 °C | 30 °C |
| 45 | 60 °C | 50 °C | 50 °C | 40 °C |
| 65 | 70 °C | 60 °C | 60 °C | 50 °C |
| 85 | 80 °C | 70 °C | 70 °C | 60 °C |
| 100 | 90 °C | 80 °C | 80 °C | 70 °C |

- Incluir uma aba Curvas na interface para editar temperaturas e boosts de cada
  perfil. Manter os limites de boost entre 30 e 100.
- Performance usa inicialmente os mesmos boosts do Normal com cada ponto
  antecipado em 10 °C, conforme aprovado pelo usuário.
- Subidas de boost acompanham a próxima leitura de temperatura, sem atraso
  intencional. Para reduzir, exigir temperatura pelo menos 3 °C abaixo da
  temperatura correspondente ao boost aplicado durante 10 segundos contínuos.
  Usar a curva interpolada para determinar essa referência; cancelar a contagem
  quando a condição deixar de ser atendida.
- A aba Curvas salva os ajustes localmente e só os aplica mediante o botão
  Salvar e aplicar. Validar temperaturas estritamente crescentes, boosts
  não decrescentes e todos os boosts entre 30 e 100 antes de salvar/aplicar.
  Editar campos por si só não altera o controle ativo.

## Implementação em 25/10/2026

- Curvas e política separadas do backend, com testes sem acesso ao hardware.
- Curvas salvas em fanctl-curves.txt junto ao executável, por substituição atômica.
- Falta/erro de temperatura interrompe o perfil e tenta restaurar; intervalo
  aceito 1..125 °C, consistente com o monitor NVML existente. NVML 30 °C é válido.
  Não detectar automaticamente valores plausíveis incorretos ou sensores travados.
- Editar uma curva ativa preserva a sessão e seu estado original; salvar
  reinicia a avaliação da curva e pode mudar o boost na próxima leitura.
- Trocar modos pelo botão Aplicar restaura a sessão anterior primeiro.
- NVML obrigatória durante perfis automáticos, retornando à opção de monitoramento
  anterior após restauração. Manual e monitoramento continuam com NVML opcional.
- Validação de hardware/visual dos novos controles ainda pendente.
