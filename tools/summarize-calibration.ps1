[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$data=Get-Content (Join-Path $root '.scratch\fan-controller\fan-calibration.json') -Raw | ConvertFrom-Json
if ($data.status -ne 'completed' -or -not $data.restorationVerified -or $data.levels.Count -ne 5) {
    throw 'Calibracao incompleta ou restauracao nao confirmada.'
}
$culture=[Globalization.CultureInfo]::GetCultureInfo('pt-BR')
$timezone=[TimeZoneInfo]::FindSystemTimeZoneById('E. South America Standard Time')
$started=[TimeZoneInfo]::ConvertTime([datetimeoffset]$data.started,$timezone).ToString('dd/MM/yyyy HH:mm:ss zzz',$culture)
$finished=[TimeZoneInfo]::ConvertTime([datetimeoffset]$data.finished,$timezone).ToString('dd/MM/yyyy HH:mm:ss zzz',$culture)
$lines=@(
    '# Calibração de boost e RPM',
    '',
    "Início: $started. Fim: $finished. Horários de São Paulo.",
    '',
    'Dell G15 5530, perfil Custom (0), os mesmos valores solicitados às duas fans.',
    'Nenhuma carga artificial foi iniciada; a atividade do computador não foi padronizada.',
    '',
    '| Boost | Fan 50: média RPM | Fan 51: média RPM | Amostras no nível | Estabilidade final |',
    '| ---: | ---: | ---: | ---: | --- |'
)
foreach ($level in $data.levels) {
    $fan50=@($level.fans | Where-Object { $_.id -eq 50 })[0]
    $fan51=@($level.fans | Where-Object { $_.id -eq 51 })[0]
    $stable=if ($level.stable) { 'Critério atingido' } else { 'Critério não atingido' }
    $lines += "| $($level.boost) | $($fan50.meanRpm.ToString('N1',$culture)) | $($fan51.meanRpm.ToString('N1',$culture)) | $($level.samples) | $stable |"
}
$lines += @(
    '',
    '## Método e interpretação',
    '',
    '- Cada nível foi observado por pelo menos 15 amostras com espera de 2 segundos entre elas; limite de 30 amostras se necessário.',
    '- As médias da tabela usam apenas as últimas cinco amostras de cada nível, após a rampa inicial.',
    '- Critério de estabilidade: amplitude dos RPM nas últimas cinco amostras menor ou igual ao maior entre 100 RPM e 5% da média, em ambas as fans.',
    '- Estabilidade de RPM não significa equilíbrio térmico. Este ensaio não mede ganho de temperatura nem desempenho.',
    '- A curva é uma referência empírica desta execução: firmware, temperatura e carga podem alterar os RPM para o mesmo boost.',
    '- Boost 0 não significa fan desligada. Boost 100 é o maior pedido nesta faixa, não uma prova independente do máximo físico da fan.',
    '',
    '## Restauração e artefatos',
    '',
    "Perfil original: $($data.original.profile). Perfil final: $($data.final.profile).",
    "Boosts originais: fan 50=$($data.original.fans[0].boost), fan 51=$($data.original.fans[1].boost).",
    "Boosts finais: fan 50=$($data.final.fans[0].boost), fan 51=$($data.final.fans[1].boost).",
    'Restauração confirmada por leitura, conforme registro bruto.',
    '',
    '- Execução: `tools/calibrate-fans.ps1`.',
    '- Dados brutos: `.scratch/fan-controller/fan-calibration.json`.',
    '- Gerar novamente este relatório: `tools/summarize-calibration.ps1`.',
    '- A primeira tentativa parou no cálculo estatístico do nível 0; restaurou o estado original. Dados preservados em `fan-calibration-attempt1.json`.',
    '- A correção estatística foi verificada por replay das amostras como hashtables PowerShell antes de repetir o ensaio.'
)
$path=Join-Path $root 'docs\fan-calibration.md'
$lines | Set-Content -LiteralPath $path -Encoding UTF8
Get-Item $path | Select-Object FullName,Length
