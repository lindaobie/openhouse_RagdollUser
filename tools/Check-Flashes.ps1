# 抓取基站真实的闪光布局，存成 flashes.txt
#
#   Check-Flashes.bat
#
# --check 模式会一直打印到按 Ctrl-C，这个脚本让它跑 12 秒然后收掉。
# 需要先完全退出 SteamVR：接收器是独占的。
$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$apps = Join-Path (Split-Path $here -Parent) 'packages\driver\openhouse-windows-x64\apps'
$exe  = Join-Path $apps 'lhcapture.exe'
$out  = Join-Path $here 'flashes.txt'
$err  = Join-Path $here 'flashes.err.txt'

if (Get-Process -Name vrserver -ErrorAction SilentlyContinue) {
    Write-Host ""
    Write-Host "  SteamVR 还在运行，接收器被它占着。" -ForegroundColor Red
    Write-Host "  请先完全退出 Steam（托盘图标右键 → 退出），再运行一次。" -ForegroundColor Red
    Write-Host ""
    exit 1
}
if (-not (Test-Path $exe)) {
    Write-Host ""
    Write-Host "  找不到 lhcapture.exe：" -ForegroundColor Red
    Write-Host "  $exe" -ForegroundColor Red
    Write-Host ""
    exit 1
}

Write-Host ""
Write-Host "  如果你的 tracker / 手柄现在没亮灯，先按一下它们的电源键嗅醒。" -ForegroundColor Yellow
Write-Host "  接收器只转发设备上报的数据，设备睡着就什么都没有。" -ForegroundColor Yellow
Write-Host ""
Start-Sleep -Seconds 3

Write-Host "  正在采集 20 秒的闪光布局，期间不要动基站……" -ForegroundColor Cyan
Remove-Item $out, $err -ErrorAction SilentlyContinue

$p = Start-Process -FilePath $exe -ArgumentList '--check' -WorkingDirectory $apps `
     -RedirectStandardOutput $out -RedirectStandardError $err -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 20
if (-not $p.HasExited) {
    try { $p.Kill() } catch { }
    Start-Sleep -Milliseconds 600
}

Write-Host "  采集结束。" -ForegroundColor Cyan
Write-Host ""
if (Test-Path $out) {
    Get-Content $out | Select-Object -Last 40
} else {
    Write-Host "  没有产生输出文件。" -ForegroundColor Yellow
}
if ((Test-Path $err) -and (Get-Item $err).Length -gt 0) {
    Write-Host ""
    Write-Host "  错误输出：" -ForegroundColor Yellow
    Get-Content $err | Select-Object -First 10
}
Write-Host ""
Write-Host "  完整内容已存到：$out" -ForegroundColor Green
Write-Host ""
