# Temperatura GPU aparece como pontos isolados

Status: ready-for-human

O gráfico instalado mostrava temperaturas GPU válidas como pontos isolados.
Reproduzir com duas amostras adjacentes: Dell 30 °C em 1792942755 e NVML 46 °C
em 1792943057. O banco pessoal foi consultado somente em modo de leitura.

## Comments

- `tools/test-history-chart.ps1` reproduziu o erro no desenho real: zero colunas
  vermelhas conectando as amostras. Mantendo somente a fonte igual, foram 309;
  removendo boost e RPM, continuaram zero. As temperaturas não eram ausentes.
- Causa: o renderer interrompia deliberadamente a linha a cada mudança de fonte;
  as fontes alternavam nas amostras do período mostrado.
- Correção: conectar leituras válidas com tracejado nas mudanças de fonte,
  mantendo lacunas para temperaturas ausentes ou intervalos de coleta perdidos.
- Regressão cobre continuidade, distinção tracejado/sólido, ausências e lacunas.
- Após a correção: 231 colunas vermelhas na transição tracejada e 309 na linha
  sólida; ausências e intervalos perdidos continuam com zero conexões indevidas.
- A suíte completa passou; a reprodução visual das sete amostras está em
  build/gpu-temperature-regression.png. Nenhum dado histórico foi reescrito.
- Pacote 0.4.2 preparado preservando as alterações 0.4.1 já presentes no projeto.
  A primeira tentativa de instalação foi impedida pelo guard enquanto a instância
  elevada continuava ativa; a ferramenta de UI não conseguiu acionar Sair.
- Após o usuário encerrar normalmente, a versão 0.4.2 foi instalada com sucesso.
  Hash do executável igual ao pacote; curvas, último modo e registros históricos
  preservados. Tarefa Running com uma instância. Evidência em
  .scratch/historico-termico/update-check-0.4.2.json. Resta apenas conferência
  visual do usuário na instalação atualizada.
