# ============================================================
# scToLocalDesk | scripts/soak_8h.ps1
# 长稳压测（Doc18 §6.3）：双实例 E2E 连续推流 N 小时
# 采样：每 60s 记录双进程 WS/PM/句柄数 → CSV；结束输出峰值摘要
# 用法：powershell -File scripts\soak_8h.ps1 -Hours 8
# 结果：build-msvc\soak_result.csv + 控制台摘要（中途退出可续看）
# ============================================================
param(
    [double]$Hours = 8,
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$widgetsExe = Join-Path $Root "build-msvc\scToLocalDeskWidgets.exe"
$qmlExe     = Join-Path $Root "build-msvc\w2\scToLocalDesk.exe"
$csvPath    = Join-Path $Root "build-msvc\soak_result.csv"

if (-not (Test-Path $widgetsExe)) { Write-Error "被控端 exe 缺失：$widgetsExe"; exit 1 }
if (-not (Test-Path $qmlExe))     { Write-Error "控制端 exe 缺失：$qmlExe"; exit 1 }

# 清残留 → 起双端（自动放行 + 自动连接，无文件钩子）
taskkill /IM scToLocalDeskWidgets.exe /F 2>$null | Out-Null
taskkill /IM scToLocalDesk.exe /F 2>$null | Out-Null
Start-Sleep -Seconds 2

$env:SCLD_SMOKE_APPROVE = '1'
$env:SCLD_SMOKE_CONNECT = 'SC-PC'
Remove-Item Env:SCLD_SMOKE_FILE -ErrorAction SilentlyContinue

Start-Process $widgetsExe -WorkingDirectory (Split-Path $widgetsExe)
Start-Sleep -Seconds 5
Start-Process $qmlExe -WorkingDirectory (Split-Path $qmlExe)

"sample_time,name,pid,ws_mb,pm_mb,handles" | Out-File $csvPath -Encoding utf8
$end = (Get-Date).AddHours($Hours)
$last = @{}
Write-Output "soak started $(Get-Date -Format 'HH:mm:ss') for $Hours h"

while ((Get-Date) -lt $end) {
    Start-Sleep -Seconds 60
    $procs = Get-Process -Name scToLocalDeskWidgets, scToLocalDesk -ErrorAction SilentlyContinue
    if (-not $procs) {
        Write-Output "ABORT: 双端进程全部消失 $(Get-Date -Format 'HH:mm:ss')"
        break
    }
    foreach ($p in $procs) {
        $ws = [math]::Round($p.WorkingSet64 / 1MB, 1)
        $pm = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)
        "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss'),$($p.ProcessName),$($p.Id),$ws,$pm,$($p.HandleCount)" |
            Add-Content $csvPath
        $key = $p.ProcessName
        if (-not $last.ContainsKey($key) -or $pm -gt $last[$key].pm) {
            $last[$key] = @{ pm = $pm; ws = $ws; h = $p.HandleCount }
        }
    }
}

# 摘要：峰值内存/句柄（内存无增长判据：末段 PM 与首段差 < 20MB）
Write-Output "=== soak summary ==="
$last.GetEnumerator() | ForEach-Object {
    Write-Output "$($_.Key): peak_pm=$($_.Value.pm)MB peak_ws=$($_.Value.ws)MB handles=$($_.Value.h)"
}
Write-Output "csv=$csvPath"

taskkill /IM scToLocalDeskWidgets.exe /F 2>$null | Out-Null
taskkill /IM scToLocalDesk.exe /F 2>$null | Out-Null
