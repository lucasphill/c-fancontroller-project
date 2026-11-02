# OSD próprio externo para jogos em janela

Para a próxima versão, o FanController terá um OSD próprio externo ao jogo, inicialmente validado no Euro Truck Simulator 2 em janela ou janela sem bordas. Essa decisão substitui a dependência obrigatória de RTSS como renderizador no ADR 0002. A implementação 0.5.0 continua usando RTSS até que a nova solução seja implementada e validada.

Uma sobreposição externa permite controlar a apresentação com um único aplicativo para instalar e abrir, sem modificar os arquivos do jogo ou carregar código nele. Em troca, não se promete suporte a tela cheia exclusiva nesta etapa. Componentes próprios podem acompanhar o instalador e ser gerenciados automaticamente. A coleta independente de FPS e tempo de quadro ainda requer validação técnica; aceitar o desenho não estabelece que essa coleta já funcione.

A prova posterior confirmou sensores e frames reais no menu do Euro Truck com RTSS fechado. A integração 0.6.0 vincula PresentData estaticamente, usa uma janela externa própria e move a aquisição/limpeza ETW para um serviço interno de thread, sem serviço Windows adicional. FPS e ms representam taxa e intervalo médio entre apresentações da aplicação; os limites da validação continuam registrados nos relatórios locais.
