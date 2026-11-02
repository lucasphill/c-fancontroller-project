# RTSS para exibição na tela

O FanController usará o RivaTuner Statistics Server (RTSS), instalado e executando na sessão do usuário, para desenhar métricas sobre os jogos pela interface disponibilizada em seu SDK. Um renderizador próprio exigiria manter integração gráfica e compatibilidade com os jogos; a escolha pelo RTSS reduz esse trabalho, em troca de uma dependência externa para o OSD.

As temperaturas, o uso e as rotações serão obtidos das fontes disponíveis do computador sem exigir MSI Afterburner aberto. FPS e tempo de quadro dependerão das leituras disponíveis e validadas no RTSS. A ausência do RTSS torna o OSD indisponível, mas não interrompe o controle de fans. A aquisição e publicação das métricas auxiliares devem preservar o funcionamento do controle térmico.
