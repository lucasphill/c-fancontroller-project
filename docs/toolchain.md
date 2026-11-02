# Ferramentas de compilação

O utilitário será desenvolvido em C++ para Windows x64.

## Componentes selecionados

- Visual Studio 2022 Build Tools, canal Current.
- Microsoft.VisualStudio.Component.VC.Tools.x86.x64.
- Microsoft.VisualStudio.Component.Windows11SDK.26100.

Instalação sem IDE completa, componentes recomendados adicionais ou reinicialização automática.
O instalador usa `--nocache` para não manter os pacotes baixados após a instalação.

## Origem e execução

Bootstrapper oficial: https://aka.ms/vs/17/release/vs_buildtools.exe

Documentação: https://learn.microsoft.com/en-us/visualstudio/install/use-command-line-parameters-to-install-visual-studio

O download fica em `%TEMP%\fancontroller-vs-buildtools.exe`. O script
`tools/install-build-tools.ps1` exige administrador e verifica a assinatura Microsoft
antes de executá-lo. O resultado fica em `.scratch/fan-controller/build-tools-install.json`.

## Verificação

Compilar `.scratch/fan-controller/toolchain-smoke.cpp` com MSVC x64 e Windows SDK,
vinculando `ole32.lib` e `wbemuuid.lib`. O programa cria um localizador COM/WMI,
sem consultar o hardware nem alterar a ventilação.

## Estado da primeira tentativa

Em 2026-10-24, assinatura do bootstrapper validada. O instalador retornou 1618
nas verificações iniciais: outra instalação em andamento e indisponibilidade do
Windows Installer. O histórico MSI mostrou uma transação Dell Core Services
iniciada antes da tentativa, sem evento de conclusão no momento da consulta.
O serviço Windows Installer estava em execução; não havia indicador CBS de reboot pendente.
Nenhum processo concorrente foi encerrado.

## Instalação concluída e validada

A segunda tentativa começou após a conclusão do SupportAssist e terminou em
2026-10-24 às 20:41 (America/Sao_Paulo), com código 0, sem reinicialização.

- Build Tools instalado em `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`.
- MSVC 14.44; Windows SDK 10.0.26100.
- `tools/build.ps1` compilou o utilitário x64 com `/W4 /WX /O2 /MT`.
- A prova COM/WMI compilou e executou com sucesso.
- `dumpbin /dependents` mostrou apenas DLLs do Windows: ole32, OLEAUT32,
  ADVAPI32 e KERNEL32. Nenhuma DLL redistribuível do runtime C++ foi exigida.
