# Modernização da interface

Status: ready-for-agent

Proposta Estúdio aprovada e implementação autorizada. A aproximação Win32/GDI foi rejeitada por divergir do layout aprovado; a interface deve reutilizar sua composição em WebView2, conforme ADR 0004.

## Decisões confirmadas

- Composição visual escolhida: **Estúdio**, com leituras em faixas e navegação lateral.
- Ao passar o cursor nos gráficos, inclusive os da Visão geral, mostrar detalhes da amostra: data e horário, temperatura, RPM, fonte da leitura, modo e perfil/revisão vigentes. Indicar lacunas sem inventar valores; em períodos agregados, distinguir resumo de medição pontual.
- Painel de acompanhamento como tela principal; análise detalhada do histórico em uma visão própria.
- Navegação lateral com Visão geral, Histórico, Perfis e curvas e Configurações; ajustes do OSD dentro de Configurações.
- Fechar a janela mantém o aplicativo na bandeja, com controle e histórico ativos. Ação separada Encerrar finaliza a sessão de controle e restaura o estado anterior.
- Painel com dois blocos, CPU e GPU: temperatura e RPM em destaque e pequeno gráfico recente abaixo. Modo de controle ativo no topo; clique no gráfico abre o histórico completo.
- Histórico completo com gráficos separados de temperatura e RPM, eixo de tempo compartilhado e cursor sincronizado; possibilidade de comparar CPU/GPU ou filtrar uma delas.
- Histórico com zoom por seleção, atalhos de 15 minutos, 1 hora, 24 horas, 7 dias e 90 dias, retorno ao período anterior e valores exatos sob o cursor.
- A atualização inclui rebranding: novo nome curto (marca própria, sigla ou acrônimo) e criação de logo. Não é obrigatório conter Fan ou Controller.
- Nome escolhido: **aeri**, sempre escrito em letras minúsculas. Disponibilidade do nome ainda não verificada.
- Logo com símbolo abstrato de fluxo de ar acompanhado de aeri em tipografia simples e arredondada; símbolo utilizável isoladamente no ícone do aplicativo e na bandeja do Windows.
- Cor principal azul esverdeado, aplicada discretamente à marca, seleções e ações principais sobre fundos escuros neutros; CPU e GPU com cores distintas nos gráficos.
- Logo inclui versão monocromática e símbolo simplificado para tamanhos pequenos, especialmente a bandeja do Windows.
- Próxima etapa: proposta visual com dados demonstrativos para Visão geral, Histórico e editor de curvas, acompanhada da logo; incluir eventos de troca de perfil e lacunas. Validar antes de escolher a tecnologia da interface definitiva.
- Identidade deve transmitir precisão e tranquilidade, com aparência premium e discreta e símbolo legível na bandeja do Windows. Nomes evocativos são aceitos, sem exigir expansão técnica das letras.
- Histórico deve marcar ativações de perfis e informar o perfil em execução no instante consultado.
- Preservar a configuração do perfil vigente naquele instante, incluindo nome e curvas, mesmo que o perfil seja editado posteriormente.
- Aplicar alterações às curvas do perfil ativo gera um novo evento no histórico, mesmo quando o nome do perfil permanece igual; registrar quando a configuração entrou em vigor.
- Edição gráfica das curvas exige botão Aplicar; arrastar pontos permite revisar uma proposta sem alterar imediatamente o controle nem gerar eventos a cada movimento.
- Editor com curvas CPU e GPU lado a lado, temperatura no eixo horizontal e boost solicitado no vertical; arrastar, adicionar e remover pontos, com temperatura e RPM atuais como referência.
- Ao sair do editor com alterações não aplicadas, oferecer Aplicar, Descartar e Continuar editando.
- Histórico registra mudanças para controle manual, automático e restauração do controle original, permitindo consultar modo e ajustes vigentes inclusive quando nenhum perfil estiver em execução.
- Períodos sem observação válida aparecem como lacunas, sem unir os pontos nem presumir continuidade do último perfil; o estado desses períodos é desconhecido.
- Temperaturas e rotações (RPM) de CPU/GPU são as informações principais. O foco são os números, sem depender de um selo genérico de saúde.
- FPS aparece apenas na faixa de monitoramento (OSD) durante jogos, sem ocupar a Visão geral.
- OSD permanece uma faixa compacta e discreta, com fundo e contraste próprios, independentes da transparência da janela principal, para temperatura, RPM e FPS legíveis sobre os jogos.
- Ajustes separados do acompanhamento, com edição das curvas de ventilação em gráfico.
- Visual sóbrio e escuro, com efeitos de vidro e prioridade à legibilidade.
- Fundo da janela com vidro real levemente translúcido e desfocado, permitindo perceber a área de trabalho e janelas atrás. Padrão com transparência, opção para desativá-la e superfícies de maior contraste sob números e gráficos. Viabilidade técnica ainda deve ser validada na escolha da interface.
- Pequeno aumento de consumo de memória é aceitável para melhorar a interface e os gráficos, desde que não prejudique jogos ou monitoramento em segundo plano.
- Histórico deve permitir investigar picos rápidos e a resposta das fans: maior detalhe recente e dados resumidos para períodos longos.
- Histórico com leituras a cada 2 segundos nas últimas 24 horas e resumos por minuto até completar 90 dias, preservando mínimos e máximos. A frequência de registro não implica uma nova leitura do sensor quando a fonte não tiver atualizado.

## Definições pendentes

- Semântica dos resumos e tratamento de lacunas, indisponibilidade e fontes de leitura.
- Composição do painel, interação dos gráficos e edição de curvas.
- Tecnologia da interface e limites de consumo verificáveis.
- Direção visual da logo e suas aplicações.

## Restrições existentes

- O histórico atual registra leituras pontuais a cada cinco minutos; essas leituras antigas não permitem reconstruir picos entre amostras.
- Preservar dados existentes e considerar o ADR 0001 (SQLite local, retenção de 90 dias).
- Distinguir boost solicitado de rotação medida, conforme GLOSSARY.md.
- Não apresentar leituras indisponíveis ou antigas como valores atuais.

## Comments

- Entrevista conduzida com grill-with-docs; usuário solicitou uma pergunta por vez.
- Usuário confirmou entendimento e autorizou a criação da proposta visual. Protótipo interativo descartável apresentado em três composições: Essencial, Estúdio e Imersiva, com dados simulados, navegação, histórico e editor. Não define a tecnologia de produção. Vidro simulado sobre fundo ilustrativo; transparência real depende da futura integração com Windows.
