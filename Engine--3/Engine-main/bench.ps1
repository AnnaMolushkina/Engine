# Замер "до/после" по методике лекции 2 (п. 2.4 ЛР1).
# Release-сборка, фиксированная сцена, прогрев, N прогонов с job system и без неё.
# Скрипт копируется рядом с Engine.exe при сборке, запускать оттуда:
#   powershell -ExecutionPolicy Bypass -File .\bench.ps1
#   powershell -ExecutionPolicy Bypass -File .\bench.ps1 -Runs 5 -Entities 50000
param(
    [string]$Exe = (Join-Path $PSScriptRoot "Engine.exe"),
    [int]$Runs = 3,
    [int]$Entities = 20000,
    [int]$Warmup = 300,
    [int]$Frames = 2000,
    [int]$Workers = 0,
    [string]$Out = "bench_results.csv"
)

$ErrorActionPreference = "Stop"
$exePath = (Resolve-Path $Exe).Path
$exeDir = Split-Path $exePath
$csvPath = Join-Path $exeDir $Out
if (Test-Path $csvPath) { Remove-Item $csvPath }

foreach ($mode in @("off", "on")) {
    for ($i = 1; $i -le $Runs; $i++) {
        Write-Host "Run $i/$Runs, jobs=$mode ..."
        $arguments = @("--bench", "--jobs=$mode", "--entities=$Entities", "--warmup=$Warmup",
                       "--frames=$Frames", "--out=$Out", "--label=jobs-$mode-run$i")
        if ($Workers -gt 0) { $arguments += "--workers=$Workers" }
        $process = Start-Process -FilePath $exePath -ArgumentList $arguments -WorkingDirectory $exeDir -PassThru -Wait
        if ($process.ExitCode -ne 0) { throw "Run failed (exit code $($process.ExitCode)), see engine.log" }
    }
}

function Get-Median([double[]]$values) {
    $sorted = $values | Sort-Object
    $n = $sorted.Count
    if ($n % 2) { return $sorted[[int][math]::Floor($n / 2)] }
    return ($sorted[$n / 2 - 1] + $sorted[$n / 2]) / 2
}

# Сводка: медиана по прогонам для каждой метрики
$rows = Import-Csv $csvPath
$summary = foreach ($group in ($rows | Group-Object metric, jobs)) {
    $first = $group.Group[0]
    [pscustomobject]@{
        metric    = $first.metric
        jobs      = $first.jobs
        threads   = $first.threads
        runs      = $group.Count
        median_ms = [math]::Round((Get-Median ($group.Group | ForEach-Object { [double]$_.median_ms })), 3)
        p95_ms    = [math]::Round((Get-Median ($group.Group | ForEach-Object { [double]$_.p95_ms })), 3)
        p99_ms    = [math]::Round((Get-Median ($group.Group | ForEach-Object { [double]$_.p99_ms })), 3)
    }
}

$table = foreach ($metric in ($summary | Select-Object -ExpandProperty metric -Unique)) {
    $off = $summary | Where-Object { $_.metric -eq $metric -and $_.jobs -eq "off" }
    $on = $summary | Where-Object { $_.metric -eq $metric -and $_.jobs -eq "on" }
    if (-not $off -or -not $on) { continue }
    [pscustomobject]@{
        metric         = $metric
        off_median_ms  = $off.median_ms
        on_median_ms   = $on.median_ms
        speedup        = if ($on.median_ms -gt 0) { [math]::Round($off.median_ms / $on.median_ms, 2) } else { 0 }
        off_p95_ms     = $off.p95_ms
        on_p95_ms      = $on.p95_ms
        off_p99_ms     = $off.p99_ms
        on_p99_ms      = $on.p99_ms
        threads_on     = $on.threads
    }
}

$table | Format-Table -AutoSize
$summaryPath = Join-Path $exeDir "bench_summary.csv"
$table | Export-Csv -NoTypeInformation -Encoding UTF8 $summaryPath
Write-Host "Raw results: $csvPath"
Write-Host "Summary:     $summaryPath"
