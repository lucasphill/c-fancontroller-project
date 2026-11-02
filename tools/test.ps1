[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'MSVC nao encontrado.' }
$build=Join-Path $root 'build'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$env:FANCTL_TEST_VCVARS=Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
$env:FANCTL_TEST_SOURCE=Join-Path $root 'tests\boost_session_test.cpp'
Push-Location $build
try {
    & $env:ComSpec /d /s /c '"call "%FANCTL_TEST_VCVARS%" >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT "%FANCTL_TEST_SOURCE%" /Fe:boost-session-test.exe"'
    if ($LASTEXITCODE -ne 0) { throw 'Compilacao dos testes falhou.' }
    & .\boost-session-test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Teste de sessao falhou.' }
    $env:FANCTL_TEST_SOURCE=Join-Path $root 'tests\gpu_temperature_test.cpp'
    & $env:ComSpec /d /s /c '"call "%FANCTL_TEST_VCVARS%" >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT "%FANCTL_TEST_SOURCE%" /Fe:gpu-temperature-test.exe"'
    if ($LASTEXITCODE -ne 0) { throw 'Compilacao dos testes GPU falhou.' }
    & .\gpu-temperature-test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Teste de fonte GPU falhou.' }
    $env:FANCTL_TEST_SOURCE=Join-Path $root 'tests\control_session_test.cpp'
    & $env:ComSpec /d /s /c '"call "%FANCTL_TEST_VCVARS%" >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT "%FANCTL_TEST_SOURCE%" /Fe:control-session-test.exe"'
    if ($LASTEXITCODE -ne 0) { throw 'Compilacao dos testes de controle compartilhado falhou.' }
    & .\control-session-test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Teste de controle compartilhado falhou.' }
    $env:FANCTL_TEST_SOURCE=Join-Path $root 'tests\automatic_profiles_test.cpp'
    & $env:ComSpec /d /s /c '"call "%FANCTL_TEST_VCVARS%" >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT "%FANCTL_TEST_SOURCE%" /Fe:automatic-profiles-test.exe"'
    if ($LASTEXITCODE -ne 0) { throw 'Compilacao dos testes de curvas falhou.' }
    & .\automatic-profiles-test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Teste de curvas falhou.' }
    $env:FANCTL_TEST_SOURCE=Join-Path $root 'tests\startup_settings_test.cpp'
    & $env:ComSpec /d /s /c '"call "%FANCTL_TEST_VCVARS%" >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT "%FANCTL_TEST_SOURCE%" /Fe:startup-settings-test.exe"'
    if ($LASTEXITCODE -ne 0) { throw 'Compilacao dos testes de inicio falhou.' }
    & .\startup-settings-test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Teste de inicio falhou.' }
    $env:FANCTL_SQLITE_SOURCE=Join-Path $root 'third_party\sqlite\sqlite3.c'
    & $env:ComSpec /d /s /c '"call "%FANCTL_TEST_VCVARS%" >nul && cl /nologo /O2 /MT /W0 /TC /DSQLITE_THREADSAFE=1 /DSQLITE_DQS=0 /DSQLITE_OMIT_LOAD_EXTENSION /c "%FANCTL_SQLITE_SOURCE%" /Fo:sqlite3-test.obj"'
    if ($LASTEXITCODE -ne 0) { throw 'Compilacao do SQLite de teste falhou.' }
    $env:FANCTL_TEST_SOURCE=Join-Path $root 'tests\thermal_history_test.cpp'
    & $env:ComSpec /d /s /c '"call "%FANCTL_TEST_VCVARS%" >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /utf-8 "%FANCTL_TEST_SOURCE%" sqlite3-test.obj /Fe:thermal-history-test.exe"'
    if ($LASTEXITCODE -ne 0) { throw 'Compilacao dos testes de historico falhou.' }
    & .\thermal-history-test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Teste de historico falhou.' }
    $env:FANCTL_TEST_SOURCE=Join-Path $root 'tests\history_service_test.cpp'
    & $env:ComSpec /d /s /c '"call "%FANCTL_TEST_VCVARS%" >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /utf-8 "%FANCTL_TEST_SOURCE%" sqlite3-test.obj /Fe:history-service-test.exe /link shell32.lib ole32.lib uuid.lib"'
    if ($LASTEXITCODE -ne 0) { throw 'Compilacao dos testes de coleta em segundo plano falhou.' }
    & .\history-service-test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Teste de coleta em segundo plano falhou.' }
    & (Join-Path $PSScriptRoot 'test-history-chart.ps1')
    foreach ($suite in @('monitoring','osd','compact_osd','frame_metrics')) {
        $env:FANCTL_TEST_SOURCE=Join-Path $root "tests\${suite}_test.cpp"
        $env:FANCTL_TEST_BINARY="${suite}-test.exe"
        & $env:ComSpec /d /s /c '"call "%FANCTL_TEST_VCVARS%" >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /utf-8 /DUNICODE /D_UNICODE "%FANCTL_TEST_SOURCE%" /Fe:"%FANCTL_TEST_BINARY%" /link user32.lib shell32.lib ole32.lib advapi32.lib uuid.lib"'
        if ($LASTEXITCODE -ne 0) { throw "Compilacao dos testes $suite falhou." }
        & (Join-Path $build $env:FANCTL_TEST_BINARY)
        if ($LASTEXITCODE -ne 0) { throw "Teste $suite falhou." }
    }
} finally {
    Pop-Location
    Remove-Item Env:FANCTL_TEST_VCVARS,Env:FANCTL_TEST_SOURCE,Env:FANCTL_TEST_BINARY,Env:FANCTL_SQLITE_SOURCE -ErrorAction SilentlyContinue
}
