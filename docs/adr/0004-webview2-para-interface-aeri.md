# WebView2 para a interface aeri

A interface aeri usa HTML, CSS e JavaScript locais em WebView2, preservando o núcleo C++ de controle, sensores, bandeja e OSD. O layout Estúdio foi aprovado em um protótipo interativo. A primeira tentativa de reproduzi-lo com controles Win32/GDI alterou proporções, tipografia e superfícies e foi rejeitada pelo usuário.

Manter somente GDI reduziria dependências, mas exigiria reconstruir manualmente apresentação, gráficos e interações com fidelidade insuficiente. Uma nova camada nativa com outro framework exigiria outra tradução do desenho. WebView2 permite reutilizar diretamente a composição aprovada, em troca de processos adicionais, memória e dependência do runtime Evergreen da Microsoft. O usuário aceitou um aumento controlado de memória; o consumo deve ser medido e a apresentação deve parar de atualizar quando a janela estiver na bandeja.

Os recursos da interface são embutidos no executável. Não há servidor HTTP, acesso de rede pela página, bibliotecas remotas nem execução de comandos arbitrários. A ponte aceita apenas operações explícitas, valida argumentos no C++ e mantém aplicação/restauração no worker existente. O SDK é fixado por versão e SHA-256; o runtime é instalado separadamente se ausente.

O efeito de fundo usa Acrylic do DWM quando disponível, com alternativa sólida. A transparência da página não se aplica ao OSD. SQLite e dados existentes continuam no caminho definido pelo ADR 0001. O rebranding muda a identidade visível sem renomear silenciosamente arquivos de sessão, banco, mutex ou tarefa de login existentes.
