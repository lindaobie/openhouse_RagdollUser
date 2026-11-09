# openhouse 驱动安装工具（Windows）
#
# 安装器会先检查 SteamVR 里现在装的是哪个驱动：
#   · 已经是 openhouse 的  → 问你要不要重装
#   · 还是原厂驱动          → 问你要不要装
# 选"是"就自动结束 Steam / SteamVR 等相关进程，装完再提示你重新启动。
#
#   Install-Driver.bat       安装
#   Uninstall-Driver.bat     还原原厂驱动
param(
    [ValidateSet('Install', 'Uninstall', 'Restore', 'Status')][string]$Action = 'Install',
    [string]$SteamVRPath = '',
    [string]$DriverPath = (Join-Path $PSScriptRoot 'driver_lighthouse.dll')
)
$ErrorActionPreference = 'Stop'
if ($Action -eq 'Restore') { $Action = 'Uninstall' }

# ---------- 找 SteamVR ----------
if (-not $SteamVRPath) {
    $pathsFile = Join-Path $env:LOCALAPPDATA 'openvr\openvrpaths.vrpath'
    if (Test-Path $pathsFile) {
        foreach ($candidate in (Get-Content -Raw $pathsFile | ConvertFrom-Json).runtime) {
            if (Test-Path (Join-Path $candidate 'drivers\lighthouse\bin\win64')) {
                $SteamVRPath = $candidate; break
            }
        }
    }
}
if (-not $SteamVRPath) {
    $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    if ($steam) { $SteamVRPath = Join-Path $steam 'steamapps\common\SteamVR' }
}
if (-not $SteamVRPath -or -not (Test-Path $SteamVRPath)) {
    Write-Host ""
    Write-Host "错误：找不到 SteamVR 的安装位置。" -ForegroundColor Red
    Write-Host "      如果装在别处，用参数指定：" -ForegroundColor Red
    Write-Host "      Install-Driver.bat -SteamVRPath `"D:\SteamLibrary\steamapps\common\SteamVR`"" -ForegroundColor Red
    exit 1
}

$directory = Join-Path $SteamVRPath 'drivers\lighthouse\bin\win64'
$target = Join-Path $directory 'driver_lighthouse.dll'
$backup = "$target.stock"
if (-not (Test-Path $target)) {
    Write-Host ""
    Write-Host "错误：这个位置没有 driver_lighthouse.dll：" -ForegroundColor Red
    Write-Host "      $target" -ForegroundColor Red
    exit 1
}

# ---------- 判断当前装的是哪个 ----------
function Get-InstalledKind {
    $bytes = [IO.File]::ReadAllBytes($target)
    $text = [Text.Encoding]::ASCII.GetString($bytes)
    if ($text.Contains('liblh session started')) { return 'openhouse' }
    return 'stock'
}

function Get-BlockingProcesses {
    @(Get-Process -Name steam,vrserver,vrmonitor,vrcompositor,monado-service -ErrorAction SilentlyContinue |
      Select-Object -ExpandProperty ProcessName -Unique)
}

# ---------- 小工具 ----------
# PowerShell 5 的 UTF8 写入会带 BOM，这里不要
function Write-Text([string]$path, [string]$text) {
    [System.IO.File]::WriteAllText($path, $text + "`n", (New-Object System.Text.UTF8Encoding($false)))
}

function Get-SteamConfig {
    $pathsFile = Join-Path $env:LOCALAPPDATA 'openvr\openvrpaths.vrpath'
    if (Test-Path $pathsFile) {
        $config = @((Get-Content -Raw $pathsFile | ConvertFrom-Json).config) | Select-Object -First 1
        if ($config) { return $config }
    }
    return Join-Path (Split-Path (Split-Path (Split-Path $SteamVRPath))) 'config'
}

$checkKey = 'openhouse.check'
$checkDir = Join-Path $env:LOCALAPPDATA 'openhouse'
$checkManifest = Join-Path $checkDir 'openhouse-check.vrmanifest'

function Set-CheckRegistration([bool]$register) {
    $config = Get-SteamConfig
    $appconfig = Join-Path $config 'appconfig.json'
    $data = [ordered]@{}
    if (Test-Path $appconfig) {
        $parsed = Get-Content -Raw $appconfig | ConvertFrom-Json
        foreach ($p in $parsed.PSObject.Properties) { $data[$p.Name] = $p.Value }
    }
    $paths = @(@($data['manifest_paths']) | Where-Object { $_ -and ($_ -ne $checkManifest) })
    if ($register) { $paths += $checkManifest }
    $data['manifest_paths'] = $paths
    New-Item -ItemType Directory -Force -Path $config | Out-Null
    Write-Text $appconfig (ConvertTo-Json $data -Depth 10)
    $app = Join-Path $config "vrappconfig\$checkKey.vrappconfig"
    if ($register) {
        New-Item -ItemType Directory -Force -Path (Split-Path $app) | Out-Null
        Write-Text $app '{ "autolaunch" : true, "last_launch_time" : "0" }'
    } elseif (Test-Path $app) {
        Remove-Item -LiteralPath $app -Force
    }
}

$schema = Join-Path (Split-Path (Split-Path $directory)) 'resources\settings\settingsschema.vrsettings'

function Install-Schema {
    $source = Join-Path $PSScriptRoot 'settingsschema.vrsettings'
    if (-not (Test-Path $source)) { return }
    New-Item -ItemType Directory -Force -Path (Split-Path $schema) | Out-Null
    if ((Test-Path $schema) -and -not (Select-String -Quiet -Path $schema -Pattern 'driver_openhouse')) {
        Copy-Item -LiteralPath $schema -Destination "$schema.stock" -Force
    }
    Copy-Item -LiteralPath $source -Destination $schema -Force
}

function Remove-Schema {
    if ((Test-Path $schema) -and (Select-String -Quiet -Path $schema -Pattern 'driver_openhouse')) {
        Remove-Item -LiteralPath $schema -Force
    }
    if (Test-Path "$schema.stock") { Move-Item -LiteralPath "$schema.stock" -Destination $schema -Force }
}

# ---------- 结束占用进程 ----------
function Stop-BlockingProcesses {
    $blocking = Get-BlockingProcesses
    if (-not $blocking) { return $true }

    Write-Host ""
    Write-Host "  以下程序正在占用 SteamVR 的驱动文件：" -ForegroundColor Yellow
    foreach ($p in $blocking) { Write-Host "      · $p" -ForegroundColor Yellow }
    Write-Host ""
    Write-Host "  装驱动要把这个文件换掉，它们不停就没法换。"
    Write-Host ""
    $ans = Read-Host "  要我自动结束它们吗？装完你再打开 Steam 就行 [Y/n]"
    if ($ans -match '^[Nn]') {
        Write-Host ""
        Write-Host "  已取消，什么都没动。" -ForegroundColor Yellow
        Write-Host "  请手动退出这些程序后再运行本脚本。"
        return $false
    }

    # 先让 Steam 自己正常退出，退不掉再强制
    if ($blocking -contains 'steam') {
        $steamExe = Join-Path (Split-Path (Split-Path (Split-Path $SteamVRPath))) 'steam.exe'
        if (-not (Test-Path $steamExe)) {
            $s = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
            if ($s) { $steamExe = Join-Path $s 'steam.exe' }
        }
        Write-Host "  正在关闭 Steam……"
        if (Test-Path $steamExe) { & $steamExe -shutdown 2>$null | Out-Null; Start-Sleep -Seconds 6 }
    }
    foreach ($n in @('vrserver', 'vrmonitor', 'vrcompositor', 'monado-service', 'steam')) {
        Stop-Process -Name $n -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Seconds 2

    $still = Get-BlockingProcesses
    if ($still) {
        Write-Host ""
        Write-Host "  这几个自己又跑起来了：$($still -join ', ')" -ForegroundColor Red
        Write-Host "  可能是开机自启，或者 Steam 托盘常驻。请关掉它们再试一次。" -ForegroundColor Red
        return $false
    }
    Write-Host "  已全部关闭。" -ForegroundColor Green
    Write-Host ""
    return $true
}

# ---------- 状态 ----------
if ($Action -eq 'Status') {
    $kind = Get-InstalledKind
    $installed = Get-Item $target
    Write-Host ""
    Write-Host "SteamVR: $SteamVRPath"
    Write-Host "驱动   : $target"
    Write-Host "当前   : $(if ($kind -eq 'openhouse') { 'openhouse' } else { '原厂 (' + $installed.VersionInfo.FileVersion + ')' })"
    Write-Host "大小   : $($installed.Length) 字节"
    Write-Host "原厂备份: $(if (Test-Path $backup) { '有' } else { '没有' })"
    Write-Host ""
    exit 0
}

# ---------- 卸载 ----------
if ($Action -eq 'Uninstall') {
    Write-Host ""
    Write-Host "===== 还原 SteamVR 原厂驱动 ====="
    Write-Host ""
    if (-not (Test-Path $backup)) {
        Write-Host "错误：找不到原厂驱动备份：" -ForegroundColor Red
        Write-Host "      $backup" -ForegroundColor Red
        Write-Host "      没有它就没法还原。可以在 Steam 里对 SteamVR 做一次「验证文件完整性」。" -ForegroundColor Red
        exit 1
    }
    if (-not (Stop-BlockingProcesses)) { exit 1 }
    Remove-Schema
    Set-CheckRegistration $false
    if (Test-Path $checkDir) { Remove-Item -LiteralPath $checkDir -Recurse -Force }
    Move-Item -LiteralPath $backup -Destination $target -Force
    Write-Host "原厂驱动已还原。你的房间和设置（%APPDATA%\lh）都还在。" -ForegroundColor Green
    Write-Host ""
    exit 0
}

# ---------- 安装 ----------
Write-Host ""
Write-Host "===== openhouse 驱动安装 ====="
Write-Host ""
Write-Host "SteamVR: $SteamVRPath"
Write-Host ""

if (-not (Test-Path $DriverPath)) {
    Write-Host "错误：安装包里没有 driver_lighthouse.dll" -ForegroundColor Red
    Write-Host "      $DriverPath" -ForegroundColor Red
    exit 1
}

$kind = Get-InstalledKind
if ($kind -eq 'openhouse') {
    $cur = (Get-FileHash $target -Algorithm SHA256).Hash
    $new = (Get-FileHash $DriverPath -Algorithm SHA256).Hash
    Write-Host "  检查结果：openhouse 驱动已经装着了。" -ForegroundColor Green
    if ($cur -eq $new) { Write-Host "  而且就是这一版（文件完全一致）。" -ForegroundColor Green }
    else { Write-Host "  不过和这个安装包里的版本不同（可能包更新了）。" -ForegroundColor Yellow }
    Write-Host ""
    $ans = Read-Host "  还要再装一遍吗？[y/N]"
    if ($ans -notmatch '^[Yy]') {
        Write-Host ""
        Write-Host "  没有改动。" -ForegroundColor Green
        Write-Host "  直接启动 SteamVR 就可以了。"
        Write-Host ""
        exit 0
    }
} else {
    Write-Host "  检查结果：现在装的是 SteamVR 原厂驱动（$(Get-Item $target | ForEach-Object { $_.VersionInfo.FileVersion })）。" -ForegroundColor Yellow
    Write-Host "  openhouse 还没有装。" -ForegroundColor Yellow
    Write-Host ""
    $ans = Read-Host "  现在安装 openhouse 驱动吗？[Y/n]"
    if ($ans -match '^[Nn]') {
        Write-Host ""
        Write-Host "  已取消，什么都没动。" -ForegroundColor Yellow
        Write-Host ""
        exit 0
    }
}

Write-Host ""
if (-not (Stop-BlockingProcesses)) { exit 1 }

# 备份原厂 DLL：装的是我们的就不覆盖旧备份（SteamVR 更新会塞回新的原厂 DLL，那个才是最新的）
if ($kind -ne 'openhouse') {
    Copy-Item -LiteralPath $target -Destination $backup -Force
    Write-Host "  已把原厂驱动备份成 driver_lighthouse.dll.stock"
} elseif (-not (Test-Path $backup)) {
    Write-Host "  错误：当前是 openhouse 驱动，但找不到原厂备份。" -ForegroundColor Red
    Write-Host "        请在 Steam 里对 SteamVR 做一次「验证文件完整性」，再运行本脚本。" -ForegroundColor Red
    exit 1
}

Copy-Item -LiteralPath $DriverPath -Destination "$target.new" -Force
Move-Item -LiteralPath "$target.new" -Destination $target -Force
if ((Get-FileHash $target).Hash -ne (Get-FileHash $DriverPath).Hash) {
    Write-Host "  错误：装进去的文件和安装包里的不一致。" -ForegroundColor Red
    exit 1
}
Write-Host "  驱动已替换。" -ForegroundColor Green

Install-Schema
Write-Host "  已写入 openhouse 设置页（SteamVR 设置里能看到）。"

$checkExe = Join-Path $PSScriptRoot 'apps\openhouse-check.exe'
if (Test-Path $checkExe) {
    New-Item -ItemType Directory -Force -Path $checkDir | Out-Null
    Copy-Item -LiteralPath $checkExe -Destination (Join-Path $checkDir 'openhouse-check.exe') -Force
    $manifest = [ordered]@{
        source = 'openhouse'
        applications = @([ordered]@{
            app_key = $checkKey
            launch_type = 'binary'
            binary_path_windows = (Join-Path $checkDir 'openhouse-check.exe')
            arguments = '"' + $directory + '"'
            is_dashboard_overlay = $true
            strings = @{ en_us = @{ name = 'openhouse update check'; description = 'Tells you when a SteamVR update replaced the openhouse driver' } }
        })
    }
    Write-Text $checkManifest (ConvertTo-Json $manifest -Depth 10)
    Set-CheckRegistration $true
    Write-Host "  已装上更新检查器（SteamVR 更新覆盖驱动时会提醒你）。"
}

Write-Host ""
Write-Host "===== 完成 =====" -ForegroundColor Green
Write-Host ""
Write-Host "  现在可以重新打开 SteamVR 了。"
Write-Host "  第一次启动会花一点时间学房间，让设备同时看到两台以上基站就行。"
Write-Host ""
Write-Host "  想确认装的是不是 openhouse，随时运行：" -ForegroundColor Cyan
Write-Host "    tools\Check-Driver.bat" -ForegroundColor Cyan
Write-Host ""
exit 0
