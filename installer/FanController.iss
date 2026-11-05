#define AppVersion "0.7.0"
[Setup]
AppId={{7C301214-1666-4DC6-A287-8B378329C68F}
AppName=aeri
AppVersion={#AppVersion}
AppPublisher=Lucas
DefaultDirName={autopf}\FanController
DisableDirPage=yes
DefaultGroupName=aeri
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=..\dist
OutputBaseFilename=aeri-Setup-{#AppVersion}-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
AppMutex=Local\FanControllerGui
CloseApplications=no
RestartApplications=no
UninstallDisplayIcon={app}\FanController.exe
SetupLogging=yes

[Languages]
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Files]
Source: "..\build\package\aeri.exe"; DestDir: "{app}"; DestName: "FanController.exe"; Flags: ignoreversion
Source: "..\build\package\PresentMon-LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\package\PresentMon-THIRD-PARTY.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "manage-startup.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\package\fanctl-curves.txt"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "..\build\package\fanctl-startup.txt"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall

Source: "..\build\package\WebView2-LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\aeri"; Filename: "{app}\FanController.exe"; WorkingDir: "{app}"
Name: "{group}\Desinstalar aeri"; Filename: "{uninstallexe}"

[Code]
function ManageStartup(Mode: String): Boolean;
var
  Code: Integer;
  Args: String;
begin
  Args := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + ExpandConstant('{app}\manage-startup.ps1') + '" -Mode ' + Mode;
  Result := Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'), Args, '', SW_HIDE, ewWaitUntilTerminated, Code);
  Result := Result and (Code = 0);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    if not ManageStartup('Install') then
      RaiseException('Não foi possível registrar a inicialização automática. Execute o instalador novamente.');
  if CurStep = ssDone then
    if not ManageStartup('Start') then
      MsgBox('Instalado, mas não foi possível iniciar agora. O programa tentará iniciar no próximo login.', mbError, MB_OK);
end;

function InitializeUninstall(): Boolean;
begin
  Result := ManageStartup('Remove');
  if not Result then
    MsgBox('Não foi possível remover a tarefa de inicialização. A desinstalação foi interrompida.', mbError, MB_OK);
end;
