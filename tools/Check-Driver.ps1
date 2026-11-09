# 检查 SteamVR 里装的是不是 openhouse 的驱动
#
#   Check-Driver.bat
#
# 只读，不改任何文件。SteamVR 更新或文件校验会把驱动覆盖回原厂，
# 这个脚本一秒钟能看出来。

$ErrorActionPreference = "Continue"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$pkgDll = Join-Path (Split-Path $here -Parent) 'packages\driver\openhouse-windows-x64\driver_lighthouse.dll'

# 找 SteamVR
$steamVR = ''
$pathsFile = Join-Path $env:LOCALAPPDATA 'openvr\openvrpaths.vrpath'
if (Test-Path $pathsFile) {
    foreach ($c in (Get-Content -Raw $pathsFile | ConvertFrom-Json).runtime) {
        if (Test-Path (Join-Path $c 'drivers\lighthouse\bin\win64')) { $steamVR = $c; break }
    }
}
if (-not $steamVR) {
    $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    if ($steam) { $steamVR = Join-Path $steam 'steamapps\common\SteamVR' }
}
if (-not $steamVR -or -not (Test-Path $steamVR)) {
    Write-Host "找不到 SteamVR 的安装位置。" -ForegroundColor Red
    exit 1
}

$dll = Join-Path $steamVR 'drivers\lighthouse\bin\win64\driver_lighthouse.dll'
Write-Host ""
Write-Host "SteamVR: $steamVR"
Write-Host ""

if (-not (Test-Path $dll)) {
    Write-Host "驱动文件不存在：$dll" -ForegroundColor Red
    exit 1
}

$installed = Get-Item $dll
$size = $installed.Length
$mtime = $installed.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')

$isOurs = $false
if (Test-Path $pkgDll) {
    $isOurs = (Get-FileHash $dll -Algorithm SHA256).Hash -eq (Get-FileHash $pkgDll -Algorithm SHA256).Hash
}

if ($isOurs) {
    Write-Host "  openhouse 驱动在位" -ForegroundColor Green
    Write-Host "    $size 字节，写入于 $mtime"
    Write-Host "    与刷写包里的文件完全一致"
} else {
    Write-Host "  装的是原厂驱动，openhouse 又一次被覆盖了" -ForegroundColor Red
    Write-Host "    $size 字节，版本 $($installed.VersionInfo.FileVersion)，写入于 $mtime"
    Write-Host ""
    Write-Host "  重新装一遍："
    Write-Host "    packages\driver\openhouse-windows-x64\Install-Driver.bat"
}

Write-Host ""
if (Test-Path "$dll.stock") {
    $s = Get-Item "$dll.stock"
    Write-Host "  原厂备份：存在（$($s.Length) 字节，$($s.LastWriteTime.ToString('yyyy-MM-dd'))）"
} else {
    Write-Host "  原厂备份：不存在。装驱动前请先确认它还在，不然没法回滚。" -ForegroundColor Yellow
}
Write-Host ""

if ($isOurs) { exit 0 } else { exit 2 }
