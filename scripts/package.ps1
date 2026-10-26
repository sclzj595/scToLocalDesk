# ============================================================
# scToLocalDesk | scripts/package.ps1  (P5.1 release packaging)
# Usage: powershell -File scripts\package.ps1 -Version v1.0.0
# Output: build-msvc\dist\scToLocalDesk-<Version>-win64.zip
# Pre-req: cmake --build build-msvc --config Release
# ============================================================
param(
    [string]$Version = "v1.0.0",
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
$build = Join-Path $Root "build-msvc"
$stage = Join-Path $build "dist\stage"
$qtBin = "F:\Apps\QT\6.5.3\msvc2019_64\bin"

$exeList = @("scToLocalDesk.exe", "scToLocalDeskWidgets.exe")
foreach ($exe in $exeList) {
    if (-not (Test-Path (Join-Path $build $exe))) {
        Write-Error "missing artifact: $exe (build first)"; exit 1
    }
}
if (-not (Test-Path (Join-Path $qtBin "windeployqt.exe"))) {
    Write-Error "windeployqt not found: $qtBin"; exit 1
}

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item $stage -ItemType Directory -Force | Out-Null

foreach ($exe in $exeList) { Copy-Item (Join-Path $build $exe) $stage }
foreach ($dll in @("avcodec-63.dll", "avutil-61.dll", "swscale-10.dll", "swresample-7.dll")) {
    Copy-Item (Join-Path $build $dll) $stage
}

& (Join-Path $qtBin "windeployqt.exe") --release --no-compiler-runtime --qmldir (Join-Path $Root "src\ui\qml") (Join-Path $stage "scToLocalDesk.exe")
if ($LASTEXITCODE -ne 0) { Write-Error "windeployqt failed"; exit 1 }
& (Join-Path $qtBin "windeployqt.exe") --release --no-compiler-runtime (Join-Path $stage "scToLocalDeskWidgets.exe")
if ($LASTEXITCODE -ne 0) { Write-Error "windeployqt widgets failed"; exit 1 }

$example = @"
[general]
alias = My-PC
run_mode = normal
view_mode = qml

[network]
ws_port = 8899
heartbeat_interval_s = 5
offline_timeout_s = 15
send_queue_max_mb = 64

[discovery]
multicast_enabled = true
multicast_addr = 239.255.53.88
static_map =

[session]
encoder = mjpeg
capture = gdi
fps = 15
quality = high
jpeg_quality = 80
session_request_timeout_s = 30

[file]
save_path =
confirm_before_transfer = true
safe_path = true
chunk_size_kb = 256
overwrite = ask

[security]
unattended_enabled = false
unattended_salt =
unattended_password_key =
unattended_verify_hash =
unattended_fail_count = 0

[log]
level = info
max_size_mb = 5
max_files = 5

[app]
auto_start = false
minimize_to_tray = true

[appearance]
theme = light
reduce_transparency = false
"@
$enc = New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Join-Path $stage "config.ini.example"), $example, $enc)

$zip = Join-Path $build "dist\scToLocalDesk-$Version-win64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
Remove-Item $stage -Recurse -Force

$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Output "package done: $zip ($size MB)"